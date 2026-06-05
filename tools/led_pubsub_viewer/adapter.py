from __future__ import annotations

from pathlib import Path
import json
import subprocess
import tempfile

from led_render import Trace
from totem_wire import encode_model

from .catalog import ANIMATION_COMMAND_MODEL, EventDefinition, command_defaults, payload_defaults


ANIMATION_COMMAND_PAYLOAD_BYTES = 32


class RenderError(RuntimeError):
    def __init__(
        self,
        message: str,
        *,
        config_path: Path | None = None,
        trace_path: Path | None = None,
        stderr: str = "",
        stdout: str = "",
    ) -> None:
        super().__init__(message)
        self.config_path = config_path
        self.trace_path = trace_path
        self.stderr = stderr
        self.stdout = stdout

    def details(self) -> str:
        parts = [str(self)]
        if self.config_path is not None:
            parts.append(f"config: {self.config_path}")
        if self.trace_path is not None:
            parts.append(f"trace: {self.trace_path}")
        if self.stderr.strip():
            parts.append("stderr:")
            parts.append(self.stderr.strip())
        if self.stdout.strip():
            parts.append("stdout:")
            parts.append(self.stdout.strip())
        return "\n".join(parts)


def _payload_array(payload: bytes) -> list[int]:
    if len(payload) > ANIMATION_COMMAND_PAYLOAD_BYTES:
        raise ValueError(
            f"animation payload is {len(payload)} bytes, max is "
            f"{ANIMATION_COMMAND_PAYLOAD_BYTES}"
        )
    return [*payload, *([0] * (ANIMATION_COMMAND_PAYLOAD_BYTES - len(payload)))]


def payload_bytes_for_event(
    event: EventDefinition,
    *,
    command_values: dict[str, object] | None = None,
    payload_values: dict[str, object] | None = None,
) -> bytes:
    command_values = command_values or {}
    payload_values = payload_values or {}

    if event.config_model is not None:
        nested_payload = encode_model(event.config_model, payload_values)
        command = command_defaults(event)
        command.update(command_values)
        command["payloadSize"] = len(nested_payload)
        command["payload"] = _payload_array(nested_payload)
        return encode_model(ANIMATION_COMMAND_MODEL, command)

    if event.payload_model == ANIMATION_COMMAND_MODEL and event.payload_template:
        command = command_defaults(event)
        command.update(command_values)
        return encode_model(ANIMATION_COMMAND_MODEL, command)

    values = payload_defaults(event)
    values.update(payload_values)
    return encode_model(event.payload_model, values)


def renderer_json_for_event(
    event: EventDefinition,
    *,
    command_values: dict[str, object] | None = None,
    payload_values: dict[str, object] | None = None,
    frames: str = "0:180",
    mode: str = "pipeline",
) -> dict[str, object]:
    if not event.renderable or event.animation_name is None:
        raise ValueError(f"{event.label} is not renderable")

    command = command_defaults(event)
    if command_values:
        command.update(command_values)
    config = payload_defaults(event)
    if payload_values:
        config.update(payload_values)

    return {
        "animation": event.animation_name,
        "duration_ms": int(command.get("lifetimeMs") or 0),
        "layer": str(command.get("layer") or "Effect"),
        "frames": frames,
        "mode": mode,
        "config": config,
    }


def render_event_trace(
    event: EventDefinition,
    *,
    command_values: dict[str, object] | None = None,
    payload_values: dict[str, object] | None = None,
    frames: str = "0:180",
    mode: str = "pipeline",
    root: str | Path | None = None,
) -> Trace:
    temp_dir = Path(tempfile.mkdtemp(prefix="totem-led-pubsub-viewer-"))
    config_path = temp_dir / "render.json"
    trace_path = temp_dir / "render.tled"
    config = renderer_json_for_event(
        event,
        command_values=command_values,
        payload_values=payload_values,
        frames=frames,
        mode=mode,
    )
    config_path.write_text(json.dumps(config, indent=2, sort_keys=True), encoding="utf-8")
    root_path = Path(root) if root is not None else Path(__file__).resolve().parents[2]
    command = [
        str(root_path / "bin" / "led-render"),
        "--config",
        str(config_path),
        "--output",
        str(trace_path),
    ]
    try:
        subprocess.run(
            command,
            cwd=root_path,
            text=True,
            capture_output=True,
            check=True,
        )
    except subprocess.CalledProcessError as exc:
        stdout = exc.stdout if isinstance(exc.stdout, str) else ""
        stderr = exc.stderr if isinstance(exc.stderr, str) else ""
        raise RenderError(
            f"renderer failed with exit code {exc.returncode}: {' '.join(command)}",
            config_path=config_path,
            trace_path=trace_path,
            stdout=stdout,
            stderr=stderr,
        ) from exc
    return Trace(trace_path)


def parse_topic(text: str, default_topic: int) -> int:
    stripped = text.strip()
    if not stripped:
        return default_topic
    return int(stripped, 0)
