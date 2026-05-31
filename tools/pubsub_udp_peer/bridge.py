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


class PubSubNotification(BaseModel):
    model_config = ConfigDict(extra="forbid")

    kind: Literal["pubsub"]
    message_type: str
    header: HeaderModel
    payload_hex: str | None = None
    payload: dict[str, Any] | None = None


class StatusEvent(BaseModel):
    model_config = ConfigDict(extra="allow")

    kind: Literal["status", "stats"]


class RawPublishCommand(BaseModel):
    model_config = ConfigDict(extra="forbid")

    topic: int = Field(ge=0, le=0xFFFFFFFF)
    traffic_class: Literal[0, 1] = 0
    payload: bytes = Field(default=b"", max_length=2048)


NotificationHandler = Callable[
    [PubSubNotification, dict[str, Any]],
    Awaitable[None],
]
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


def parse_topic_value(value: Any) -> int:
    if isinstance(value, int):
        topic = value
    elif isinstance(value, str):
        topic = parse_int_arg(value)
    else:
        raise ValueError("topic must be an integer or integer string")
    if topic < 0 or topic > 0xFFFFFFFF:
        raise ValueError("topic must fit in uint32")
    return topic


def parse_traffic_class_value(value: Any) -> Literal[0, 1]:
    if value is None:
        return 0
    if isinstance(value, int):
        traffic_class = value
    elif isinstance(value, str):
        traffic_class = parse_int_arg(value)
    else:
        raise ValueError("traffic_class must be 0 or 1")
    if traffic_class not in {0, 1}:
        raise ValueError("traffic_class must be 0 or 1")
    return traffic_class  # type: ignore[return-value]


class LocalFanout:
    def __init__(self, bridge: "PubSubBridge", path: Path, max_buffer: int) -> None:
        self._bridge = bridge
        self._path = path
        self._max_buffer = max_buffer
        self._server: asyncio.AbstractServer | None = None
        self._clients: dict[asyncio.StreamWriter, set[int]] = {}
        self._topic_refs: dict[int, int] = {}
        self._log = logging.getLogger("pubsub_udp_peer.local")

    async def start(self) -> None:
        if self._path.exists():
            self._path.unlink()
        self._server = await asyncio.start_unix_server(
            self._handle_client,
            path=str(self._path),
        )
        self._log.info("local PubSub fanout socket listening at %s", self._path)

    async def stop(self) -> None:
        if self._server is not None:
            self._server.close()
            with contextlib.suppress(TimeoutError):
                await asyncio.wait_for(self._server.wait_closed(), timeout=0.5)
            self._server = None
        for writer in list(self._clients):
            await self._close_client(writer, send_unsubscribe=False)
        with contextlib.suppress(FileNotFoundError):
            self._path.unlink()

    async def on_notification(
        self,
        notification: PubSubNotification,
        data: dict[str, Any],
    ) -> None:
        topic = notification.header.topic
        encoded = (json.dumps(data, separators=(",", ":")) + "\n").encode(
            "utf-8"
        )
        for writer, subscriptions in list(self._clients.items()):
            if not any((topic & mask) != 0 for mask in subscriptions):
                continue
            transport = writer.transport
            if (
                transport is not None
                and transport.get_write_buffer_size() > self._max_buffer
            ):
                self._log.warning("dropping local PubSub event for slow client")
                continue
            writer.write(encoded)

    async def _handle_client(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
    ) -> None:
        self._clients[writer] = set()
        await self._write_local(writer, {"kind": "local", "event": "connected"})
        try:
            async for raw_line in reader:
                line = raw_line.decode("utf-8", "replace").strip()
                if line:
                    await self._handle_client_line(writer, line)
        finally:
            await self._close_client(writer, send_unsubscribe=True)

    async def _handle_client_line(
        self,
        writer: asyncio.StreamWriter,
        line: str,
    ) -> None:
        try:
            data = json.loads(line)
            op = data.get("op")
        except json.JSONDecodeError as exc:
            await self._write_error(writer, str(exc))
            return

        try:
            if op == "subscribe":
                await self._subscribe(writer, parse_topic_value(data.get("topic")))
                return
            if op == "unsubscribe":
                await self._unsubscribe(
                    writer,
                    parse_topic_value(data.get("topic")),
                    send_unsubscribe=True,
                )
                return
            if op == "publish":
                await self._publish(writer, data)
                return
        except ValueError as exc:
            await self._write_error(writer, str(exc))
            return

        await self._write_error(writer, f"unknown op {op!r}")

    async def _subscribe(self, writer: asyncio.StreamWriter, topic: int) -> None:
        subscriptions = self._clients[writer]
        if topic in subscriptions:
            await self._write_local(
                writer, {"kind": "local", "event": "subscribed", "topic": topic}
            )
            return
        subscriptions.add(topic)
        previous = self._topic_refs.get(topic, 0)
        self._topic_refs[topic] = previous + 1
        if previous == 0 and not self._bridge.has_persistent_subscription(topic):
            await self._bridge.subscribe_topic(topic)
        await self._write_local(
            writer, {"kind": "local", "event": "subscribed", "topic": topic}
        )

    async def _unsubscribe(
        self,
        writer: asyncio.StreamWriter,
        topic: int,
        *,
        send_unsubscribe: bool,
    ) -> None:
        subscriptions = self._clients.get(writer)
        if subscriptions is None or topic not in subscriptions:
            await self._write_local(
                writer, {"kind": "local", "event": "unsubscribed", "topic": topic}
            )
            return
        subscriptions.remove(topic)
        remaining = max(0, self._topic_refs.get(topic, 0) - 1)
        if remaining == 0:
            self._topic_refs.pop(topic, None)
            if send_unsubscribe and not self._bridge.has_persistent_subscription(topic):
                await self._bridge.unsubscribe_topic(topic)
        else:
            self._topic_refs[topic] = remaining
        await self._write_local(
            writer, {"kind": "local", "event": "unsubscribed", "topic": topic}
        )

    async def _publish(
        self,
        writer: asyncio.StreamWriter,
        data: dict[str, Any],
    ) -> None:
        topic = parse_topic_value(data.get("topic"))
        traffic_class = parse_traffic_class_value(data.get("traffic_class"))
        payload_hex = str(data.get("payload_hex", ""))
        try:
            payload = bytes.fromhex(payload_hex)
        except ValueError as exc:
            raise ValueError("payload_hex must contain hex bytes") from exc
        await self._bridge.publish_raw(
            topic=topic,
            traffic_class=traffic_class,
            payload=payload,
        )
        await self._write_local(
            writer, {"kind": "local", "event": "published", "topic": topic}
        )

    async def _close_client(
        self,
        writer: asyncio.StreamWriter,
        *,
        send_unsubscribe: bool,
    ) -> None:
        topics = list(self._clients.get(writer, set()))
        for topic in topics:
            remaining = max(0, self._topic_refs.get(topic, 0) - 1)
            if remaining == 0:
                self._topic_refs.pop(topic, None)
                if send_unsubscribe and not self._bridge.has_persistent_subscription(
                    topic
                ):
                    await self._bridge.unsubscribe_topic(topic)
            else:
                self._topic_refs[topic] = remaining
        self._clients.pop(writer, None)
        writer.close()
        with contextlib.suppress(BrokenPipeError, ConnectionResetError, TimeoutError):
            await asyncio.wait_for(writer.wait_closed(), timeout=0.5)

    async def _write_error(self, writer: asyncio.StreamWriter, detail: str) -> None:
        await self._write_local(
            writer,
            {"kind": "local", "event": "error", "detail": detail},
        )

    @staticmethod
    async def _write_local(
        writer: asyncio.StreamWriter,
        payload: dict[str, Any],
    ) -> None:
        writer.write(
            (json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8")
        )
        await writer.drain()


class PubSubBridge:
    def __init__(self, cpp_peer: Path, args: argparse.Namespace) -> None:
        self._cpp_peer = cpp_peer
        self._args = args
        self._notification_handlers: list[NotificationHandler] = []
        self._log = logging.getLogger("pubsub_udp_peer")
        self._stdin: asyncio.StreamWriter | None = None
        self._write_lock = asyncio.Lock()
        self._persistent_topics = set(args.subscribe_topic)

    def on_notification(self, handler: NotificationHandler) -> None:
        self._notification_handlers.append(handler)

    def has_persistent_subscription(self, topic: int) -> bool:
        return any(
            (topic & persistent) == topic for persistent in self._persistent_topics
        )

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

        fanout: LocalFanout | None = None
        if self._args.local_socket is not None:
            fanout = LocalFanout(
                self,
                self._args.local_socket,
                self._args.local_client_buffer,
            )
            self.on_notification(fanout.on_notification)
            await fanout.start()

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
            if fanout is not None:
                await fanout.stop()
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
        async with self._write_lock:
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
        for handler in self._notification_handlers:
            await handler(notification, data)


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
    parser.add_argument(
        "--local-socket",
        type=Path,
        default=None,
        metavar="PATH",
        help=(
            "Expose arbitrary PubSub events to local scripts as raw newline "
            "JSON over a Unix-domain socket."
        ),
    )
    parser.add_argument(
        "--local-client-buffer",
        type=int,
        default=65536,
        help="Drop forwarded events for a local client above this buffer size.",
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
