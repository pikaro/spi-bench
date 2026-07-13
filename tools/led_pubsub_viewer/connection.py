from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import os
import signal
import subprocess
import tempfile
import time

from pubsub_audio_viewer.client import LocalPubSubClient


@dataclass(frozen=True)
class ConnectionState:
    status: str
    detail: str = ""


class PubSubConnection:
    def __init__(self, root: Path | None = None) -> None:
        self._root = root or Path(__file__).resolve().parents[2]
        self._process: subprocess.Popen[str] | None = None
        self._client: LocalPubSubClient | None = None
        self._socket_path: Path | None = None
        self._state = ConnectionState("disconnected")
        self._last_event_monotonic = 0.0
        self._mcu_seen = False

    @property
    def state(self) -> ConnectionState:
        return self._state

    def connect(self, mcu_ip: str) -> None:
        self.disconnect()
        fd, socket_name = tempfile.mkstemp(prefix="totem-led-pubsub-", suffix=".sock")
        os.close(fd)
        self._socket_path = Path(socket_name)
        self._socket_path.unlink(missing_ok=True)
        command = [
            str(self._root / "bin" / "pubsub-udp-peer"),
            "--mcu-ip",
            mcu_ip,
            "--local-socket",
            str(self._socket_path),
            "--no-stdin",
        ]
        self._process = subprocess.Popen(
            command,
            cwd=self._root,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True,
        )
        self._client = LocalPubSubClient(self._socket_path, ())
        self._state = ConnectionState("starting", f"bridge pid {self._process.pid}")
        self._mcu_seen = False

    def disconnect(self) -> None:
        if self._client is not None:
            self._client.close()
            self._client = None
        if self._process is not None:
            self._terminate_process_group(self._process)
            self._process = None
        if self._socket_path is not None:
            self._socket_path.unlink(missing_ok=True)
            self._socket_path = None
        self._state = ConnectionState("disconnected")
        self._last_event_monotonic = 0.0
        self._mcu_seen = False

    def _terminate_process_group(self, process: subprocess.Popen[str]) -> None:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            return
        except OSError:
            if process.poll() is None:
                process.terminate()
        try:
            if process.poll() is None:
                process.wait(timeout=1.0)
            return
        except subprocess.TimeoutExpired:
            pass

        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            return
        except OSError:
            if process.poll() is None:
                process.kill()
        if process.poll() is None:
            process.wait(timeout=1.0)

    def poll(self) -> ConnectionState:
        if self._process is None:
            return self._state
        returncode = self._process.poll()
        if returncode is not None:
            detail = self._read_stderr_tail()
            self.disconnect()
            message = f"bridge exited {returncode}"
            if detail:
                message = f"{message}\n{detail}"
            self._state = ConnectionState("error", message)
            return self._state

        if self._client is not None and self._state.status == "starting":
            try:
                self._client.connect(timeout_s=0.02)
                self._state = ConnectionState(
                    "bridge-ready",
                    "local bridge ready; MCU not confirmed",
                )
            except RuntimeError:
                return self._state

        if self._client is not None and self._state.status in {
            "bridge-ready",
            "mcu-seen",
        }:
            try:
                events = self._client.poll()
            except RuntimeError as exc:
                self._state = ConnectionState("error", str(exc))
                return self._state
            self._handle_local_events(events)
        return self._state

    def publish(self, *, topic: int, payload: bytes, traffic_class: int = 0) -> None:
        if self._client is None or self._state.status not in {"bridge-ready", "mcu-seen"}:
            raise RuntimeError("PubSub bridge local socket is not connected")
        self._client.publish(topic=topic, payload=payload, traffic_class=traffic_class)
        if self._mcu_seen:
            detail = f"published {len(payload)} bytes; MCU seen earlier"
            self._state = ConnectionState("mcu-seen", detail)
        else:
            detail = f"published {len(payload)} bytes; MCU not confirmed"
            self._state = ConnectionState("bridge-ready", detail)

    def _handle_local_events(self, events: list[dict[str, object]]) -> None:
        for event in events:
            kind = event.get("kind")
            if kind == "pubsub":
                self._mark_mcu_seen(str(event.get("message_type", "pubsub")))
                continue
            if kind == "status":
                self._handle_status_event(event)
                continue
            if kind == "stats":
                self._handle_stats_event(event)

    def _handle_status_event(self, event: dict[str, object]) -> None:
        name = str(event.get("event", "status"))
        detail = str(event.get("detail", ""))
        if name == "keepalive-rx":
            self._mark_mcu_seen("keepalive response")
        elif name in {"publish-send-error", "send-error", "subscription-send-error"}:
            message = f"{name}: {detail}" if detail else name
            self._state = ConnectionState("error", message)
        elif name == "published":
            if self._mcu_seen:
                self._state = ConnectionState("mcu-seen", "publish sent")
            else:
                self._state = ConnectionState("bridge-ready", "publish sent; MCU not confirmed")
        elif name == "started" and not self._mcu_seen:
            self._state = ConnectionState(
                "bridge-ready",
                "UDP peer started; waiting for MCU keepalive",
            )

    def _handle_stats_event(self, event: dict[str, object]) -> None:
        keepalive_rx = int(event.get("keepalive_rx", 0) or 0)
        frames_rx = int(event.get("frames_rx", 0) or 0)
        if keepalive_rx > 0 or frames_rx > 0:
            self._mark_mcu_seen(f"rx keepalive={keepalive_rx} frames={frames_rx}")

    def _mark_mcu_seen(self, detail: str) -> None:
        self._mcu_seen = True
        self._last_event_monotonic = time.monotonic()
        self._state = ConnectionState("mcu-seen", detail)

    def _read_stderr_tail(self, limit: int = 4000) -> str:
        process = self._process
        if process is None or process.stderr is None:
            return ""
        try:
            data = process.stderr.read()
        except OSError:
            return ""
        lines = [line.rstrip() for line in data.splitlines() if line.strip()]
        text = "\n".join(lines)
        if len(text) <= limit:
            return text
        return "...\n" + text[-limit:]
