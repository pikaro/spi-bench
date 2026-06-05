from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import os
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
        )
        self._client = LocalPubSubClient(self._socket_path, ())
        self._state = ConnectionState("starting", f"bridge pid {self._process.pid}")

    def disconnect(self) -> None:
        if self._client is not None:
            self._client.close()
            self._client = None
        if self._process is not None:
            self._process.terminate()
            try:
                self._process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                self._process.kill()
                self._process.wait(timeout=1.0)
            self._process = None
        if self._socket_path is not None:
            self._socket_path.unlink(missing_ok=True)
            self._socket_path = None
        self._state = ConnectionState("disconnected")
        self._last_event_monotonic = 0.0

    def poll(self) -> ConnectionState:
        if self._process is None:
            return self._state
        returncode = self._process.poll()
        if returncode is not None:
            detail = self._read_stderr_tail()
            self.disconnect()
            self._state = ConnectionState("error", f"bridge exited {returncode}: {detail}")
            return self._state

        if self._client is not None and self._state.status == "starting":
            try:
                self._client.connect(timeout_s=0.02)
                self._state = ConnectionState("bridge-local", "local socket connected")
            except RuntimeError:
                return self._state

        if self._client is not None and self._state.status in {"bridge-local", "traffic-seen"}:
            try:
                events = self._client.poll()
            except RuntimeError as exc:
                self._state = ConnectionState("error", str(exc))
                return self._state
            if events:
                self._last_event_monotonic = time.monotonic()
                self._state = ConnectionState("traffic-seen", events[-1].get("event", "event"))
            elif (
                self._last_event_monotonic
                and time.monotonic() - self._last_event_monotonic > 5.0
            ):
                self._state = ConnectionState("bridge-local", "local socket connected")
        return self._state

    def publish(self, *, topic: int, payload: bytes, traffic_class: int = 0) -> None:
        if self._client is None or self._state.status not in {"bridge-local", "traffic-seen"}:
            raise RuntimeError("PubSub bridge local socket is not connected")
        self._client.publish(topic=topic, payload=payload, traffic_class=traffic_class)
        self._state = ConnectionState("traffic-seen", f"published {len(payload)} bytes")

    def _read_stderr_tail(self) -> str:
        process = self._process
        if process is None or process.stderr is None:
            return ""
        try:
            data = process.stderr.read()
        except OSError:
            return ""
        return " ".join(data.split())[-240:]
