from __future__ import annotations

import json
import socket
import time
from pathlib import Path
from typing import Any


class LocalPubSubClient:
    def __init__(self, path: Path, topics: tuple[int, ...]) -> None:
        self._path = path
        self._topics = topics
        self._socket: socket.socket | None = None
        self._rx = bytearray()

    def connect(self, timeout_s: float) -> None:
        deadline = time.monotonic() + timeout_s
        last_error: OSError | None = None
        while time.monotonic() < deadline:
            client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                client.connect(str(self._path))
                client.setblocking(False)
                self._socket = client
                for topic in self._topics:
                    self._send({"op": "subscribe", "topic": topic})
                return
            except OSError as exc:
                last_error = exc
                client.close()
                time.sleep(0.05)
        detail = f": {last_error}" if last_error is not None else ""
        raise RuntimeError(f"timed out connecting to {self._path}{detail}")

    def poll(self, max_events: int = 128) -> list[dict[str, Any]]:
        if self._socket is None:
            return []
        while True:
            try:
                chunk = self._socket.recv(16384)
            except BlockingIOError:
                break
            if not chunk:
                raise RuntimeError("local PubSub socket closed")
            self._rx.extend(chunk)

        events: list[dict[str, Any]] = []
        while len(events) < max_events:
            newline = self._rx.find(b"\n")
            if newline < 0:
                break
            line = bytes(self._rx[:newline])
            del self._rx[: newline + 1]
            if not line.strip():
                continue
            events.append(json.loads(line.decode("utf-8")))
        return events

    def close(self) -> None:
        if self._socket is None:
            return
        for topic in self._topics:
            try:
                self._send({"op": "unsubscribe", "topic": topic})
            except OSError:
                break
        self._socket.close()
        self._socket = None

    def publish(self, *, topic: int, payload: bytes, traffic_class: int = 0) -> None:
        self._send(
            {
                "op": "publish",
                "topic": topic,
                "traffic_class": traffic_class,
                "payload_hex": payload.hex(),
            }
        )

    def _send(self, payload: dict[str, Any]) -> None:
        if self._socket is None:
            raise RuntimeError("local PubSub socket is not connected")
        line = (json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8")
        previous_timeout = self._socket.gettimeout()
        self._socket.settimeout(1.0)
        try:
            self._socket.sendall(line)
        finally:
            self._socket.settimeout(previous_timeout)
