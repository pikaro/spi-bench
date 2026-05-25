from __future__ import annotations

import argparse
import asyncio
import contextlib
import json
import logging
import os
import re
import subprocess
import sys
from collections.abc import Awaitable, Callable
from pathlib import Path
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field, ValidationError


class HeaderModel(BaseModel):
    model_config = ConfigDict(extra="forbid")

    timestamp_ms: int
    timestamp_us: int
    message_id: int
    topic: int
    source: int
    traffic_class: int
    payload_size: int


class ButtonEventModel(BaseModel):
    model_config = ConfigDict(extra="forbid")

    type: Literal["Pressed", "Released", "Unknown"]
    button: Literal["Bell", "Unknown"]


class PubSubEventModel(BaseModel):
    model_config = ConfigDict(extra="forbid")

    topic: int
    type: Literal["Register", "Unregister", "Unknown"]


class PubSubNotification(BaseModel):
    model_config = ConfigDict(extra="forbid")

    kind: Literal["pubsub"]
    message_type: str
    header: HeaderModel
    payload: dict[str, Any] | None = None


class StatusEvent(BaseModel):
    model_config = ConfigDict(extra="allow")

    kind: Literal["status", "stats"]


class RawPublishCommand(BaseModel):
    model_config = ConfigDict(extra="forbid")

    topic: int = Field(ge=0, le=0xFFFFFFFF)
    traffic_class: Literal[0, 1] = 0
    payload: bytes = Field(default=b"", max_length=2048)


ButtonHandler = Callable[[HeaderModel, ButtonEventModel], Awaitable[None]]
_DURATION_RE = re.compile(r"^(?P<value>[1-9][0-9]*)(?P<unit>ms|s|m)?$")


def repo_root() -> Path:
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        check=True,
        capture_output=True,
        text=True,
    )
    return Path(result.stdout.strip())


def reexec_in_venv(root: Path) -> None:
    venv_python = root / ".venv" / "bin" / "python"
    if not venv_python.exists():
        return
    if os.environ.get("TOTEM_PUBSUB_UDP_PEER_VENV") == "1":
        return
    if Path(sys.prefix).resolve() == (root / ".venv").resolve():
        return

    os.environ["TOTEM_PUBSUB_UDP_PEER_VENV"] = "1"
    os.execv(str(venv_python), [str(venv_python), *sys.argv])


def build_cpp_peer(root: Path) -> Path:
    result = subprocess.run(
        [str(root / "bin" / "pubsub-udp-peer-cpp-build")],
        check=True,
        capture_output=True,
        text=True,
    )
    return Path(result.stdout.strip())


def parse_duration_ms(text: str) -> int:
    match = _DURATION_RE.match(text)
    if match is None:
        raise argparse.ArgumentTypeError(
            "duration must be a positive integer with optional ms, s, or m suffix"
        )
    value = int(match.group("value"))
    unit = match.group("unit") or "s"
    scale = {
        "ms": 1,
        "s": 1000,
        "m": 60_000,
    }[unit]
    return value * scale


def parse_int_arg(text: str) -> int:
    try:
        return int(text, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid integer: {text}") from exc


def parse_raw_publish_arg(values: list[str]) -> RawPublishCommand:
    topic_text, traffic_class_text, payload_hex = values
    try:
        payload = bytes.fromhex(payload_hex)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("payload must be hex bytes") from exc
    return RawPublishCommand(
        topic=parse_int_arg(topic_text),
        traffic_class=parse_int_arg(traffic_class_text),
        payload=payload,
    )


class PubSubBridge:
    def __init__(self, cpp_peer: Path, args: argparse.Namespace) -> None:
        self._cpp_peer = cpp_peer
        self._args = args
        self._button_handlers: list[ButtonHandler] = []
        self._log = logging.getLogger("pubsub_udp_peer")
        self._stdin: asyncio.StreamWriter | None = None

    def on_button(self, handler: ButtonHandler) -> None:
        self._button_handlers.append(handler)

    async def publish_raw(
        self,
        *,
        topic: int,
        payload: bytes = b"",
        traffic_class: Literal[0, 1] = 0,
    ) -> None:
        command = RawPublishCommand(
            topic=topic,
            payload=payload,
            traffic_class=traffic_class,
        )
        await self._write_command(
            "publish "
            f"{command.topic} {command.traffic_class} "
            f"{command.payload.hex()}\n"
        )

    async def subscribe_topic(self, topic: int) -> None:
        await self._write_command(f"subscribe {topic}\n")

    async def unsubscribe_topic(self, topic: int) -> None:
        await self._write_command(f"unsubscribe {topic}\n")

    async def run(self) -> int:
        command = [
            str(self._cpp_peer),
            "--mcu-ip",
            self._args.mcu_ip,
            "--bind-ip",
            self._args.bind_ip,
            "--port",
            str(self._args.port),
            "--keepalive-ms",
            str(self._args.keepalive_ms),
        ]
        if self._args.timeout_ms is not None:
            command.extend(["--timeout-ms", str(self._args.timeout_ms)])
        if not self._args.subscribe_button:
            command.append("--no-button-sub")

        process = await asyncio.create_subprocess_exec(
            *command,
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        self._stdin = process.stdin
        assert process.stdin is not None
        assert process.stdout is not None
        assert process.stderr is not None

        stderr_task = asyncio.create_task(self._drain_stderr(process.stderr))
        startup_task = asyncio.create_task(self._send_startup_commands())
        stdin_task = (
            asyncio.create_task(self._forward_stdin())
            if self._args.forward_stdin
            else None
        )
        try:
            async for raw_line in process.stdout:
                await self._handle_line(raw_line.decode("utf-8", "replace").strip())
        except asyncio.CancelledError:
            await self._stop_process(process, "cancelled")
            raise
        finally:
            if not startup_task.done():
                startup_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await startup_task
            if stdin_task is not None:
                stdin_task.cancel()
                with contextlib.suppress(asyncio.CancelledError):
                    await stdin_task
            await self._close_stdin()
            if process.returncode is None:
                try:
                    await asyncio.wait_for(process.wait(), timeout=0.1)
                except TimeoutError:
                    await self._stop_process(process, "teardown")
            self._stdin = None
            await stderr_task
        return process.returncode if process.returncode is not None else 1

    async def _close_stdin(self) -> None:
        if self._stdin is None:
            return
        self._stdin.close()
        try:
            await self._stdin.wait_closed()
        except (BrokenPipeError, ConnectionResetError):
            pass

    async def _stop_process(
        self,
        process: asyncio.subprocess.Process,
        reason: str,
    ) -> None:
        if process.returncode is not None:
            return
        self._log.info("stopping C++ PubSub peer: %s", reason)
        await self._close_stdin()
        process.terminate()
        try:
            await asyncio.wait_for(process.wait(), timeout=2.0)
            return
        except TimeoutError:
            self._log.warning("C++ PubSub peer did not terminate; killing")
        process.kill()
        await process.wait()

    async def _write_command(self, command: str) -> None:
        if self._stdin is None:
            raise RuntimeError("C++ PubSub peer is not running")
        self._stdin.write(command.encode("utf-8"))
        await self._stdin.drain()

    async def _send_startup_commands(self) -> None:
        for topic in self._args.subscribe_topic:
            await self.subscribe_topic(topic)
        for topic in self._args.unsubscribe_topic:
            await self.unsubscribe_topic(topic)
        for command in self._args.publish_raw:
            await self.publish_raw(
                topic=command.topic,
                traffic_class=command.traffic_class,
                payload=command.payload,
            )
        for command in self._args.send:
            await self._write_command(command.rstrip("\n") + "\n")

    async def _forward_stdin(self) -> None:
        reader = asyncio.StreamReader()
        protocol = asyncio.StreamReaderProtocol(reader)
        loop = asyncio.get_running_loop()
        try:
            await loop.connect_read_pipe(lambda: protocol, sys.stdin)
        except Exception as exc:  # pragma: no cover - platform/terminal specific
            self._log.warning("stdin forwarding unavailable: %s", exc)
            return

        async for raw_line in reader:
            line = raw_line.decode("utf-8", "replace")
            if line.strip() in {"quit", "exit", "stop"}:
                await self._write_command(line)
                return
            await self._write_command(line)

    async def _drain_stderr(self, stream: asyncio.StreamReader) -> None:
        async for raw_line in stream:
            line = raw_line.decode("utf-8", "replace").strip()
            if line:
                self._log.warning("cpp: %s", line)

    async def _handle_line(self, line: str) -> None:
        if not line:
            return
        try:
            data = json.loads(line)
        except json.JSONDecodeError:
            self._log.warning("non-json cpp output: %s", line)
            return

        kind = data.get("kind")
        if kind == "status":
            event = data.get("event", "unknown")
            detail = data.get("detail")
            if detail:
                self._log.info("status %s: %s", event, detail)
            else:
                self._log.info("status %s", event)
            return
        if kind == "stats":
            self._log.info("stats %s", data)
            return
        if kind != "pubsub":
            self._log.debug("ignored event: %s", data)
            return

        notification = PubSubNotification.model_validate(data)
        if notification.message_type == "Totem.Buttons.ButtonEvent":
            payload = ButtonEventModel.model_validate(notification.payload)
            for handler in self._button_handlers:
                await handler(notification.header, payload)


async def log_bell_event(header: HeaderModel, event: ButtonEventModel) -> None:
    if event.button != "Bell":
        return
    logging.getLogger("pubsub_udp_peer").info(
        "bell event type=%s message_id=%s source=%s",
        event.type,
        header.message_id,
        header.source,
    )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the host UDP PubSub peer and Python notify bridge.",
    )
    parser.add_argument("--mcu-ip", required=True, help="MCU IPv4 address")
    parser.add_argument(
        "--bind-ip",
        default="0.0.0.0",
        help="Local IPv4 address to bind. Use 192.168.179.6 on the guest network.",
    )
    parser.add_argument("--port", type=int, default=2026)
    parser.add_argument("--keepalive-ms", type=int, default=1000)
    parser.add_argument(
        "--timeout",
        dest="timeout_ms",
        type=parse_duration_ms,
        default=None,
        help="Stop after a duration such as 500ms, 10s, or 2m.",
    )
    parser.add_argument(
        "--publish-raw",
        action="append",
        default=[],
        nargs=3,
        metavar=("TOPIC", "CLASS", "PAYLOAD_HEX"),
        help="Publish one raw host PubSub frame after startup. Repeatable.",
    )
    parser.add_argument(
        "--subscribe-topic",
        action="append",
        default=[],
        type=parse_int_arg,
        metavar="TOPIC",
        help="Send an additional PubSub subscription control frame. Repeatable.",
    )
    parser.add_argument(
        "--unsubscribe-topic",
        action="append",
        default=[],
        type=parse_int_arg,
        metavar="TOPIC",
        help="Send an unsubscribe control frame. Repeatable.",
    )
    parser.add_argument(
        "--send",
        action="append",
        default=[],
        metavar="COMMAND",
        help="Send a raw C++ peer command after startup. Repeatable.",
    )
    stdin_group = parser.add_mutually_exclusive_group()
    stdin_group.add_argument(
        "--stdin",
        dest="forward_stdin",
        action="store_true",
        default=None,
        help="Forward this process's stdin to the C++ peer command channel.",
    )
    stdin_group.add_argument(
        "--no-stdin",
        dest="forward_stdin",
        action="store_false",
        help="Do not forward stdin, even when attached to a terminal.",
    )
    parser.add_argument(
        "--no-button-sub",
        dest="subscribe_button",
        action="store_false",
        default=True,
    )
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)
    try:
        args.publish_raw = [
            parse_raw_publish_arg(values) for values in args.publish_raw
        ]
    except (argparse.ArgumentTypeError, ValidationError) as exc:
        parser.error(str(exc))
    if args.forward_stdin is None:
        args.forward_stdin = sys.stdin.isatty()
    return args


async def amain(argv: list[str]) -> int:
    args = parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )

    root = repo_root()
    cpp_peer = build_cpp_peer(root)
    bridge = PubSubBridge(cpp_peer, args)
    bridge.on_button(log_bell_event)
    return await bridge.run()


def main(argv: list[str] | None = None) -> int:
    root = repo_root()
    reexec_in_venv(root)
    try:
        return asyncio.run(amain(sys.argv[1:] if argv is None else argv))
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
