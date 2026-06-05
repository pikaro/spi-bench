from __future__ import annotations

import argparse
import json
import sys
from typing import Any

from totem_wire import ENUMS, MODELS

from .adapter import payload_bytes_for_event, renderer_json_for_event
from .catalog import all_events, command_defaults, event_by_label, payload_defaults


def _coerce_value(text: str) -> object:
    lowered = text.lower()
    if lowered == "true":
        return True
    if lowered == "false":
        return False
    try:
        return int(text, 0)
    except ValueError:
        return text


def _set_nested(target: dict[str, Any], path: list[str], value: object) -> None:
    cursor = target
    for item in path[:-1]:
        child = cursor.setdefault(item, {})
        if not isinstance(child, dict):
            raise ValueError(f"{'.'.join(path)} conflicts with scalar assignment")
        cursor = child
    cursor[path[-1]] = value


def _split_assignments(assignments: list[str]) -> tuple[dict[str, Any], dict[str, Any]]:
    command: dict[str, Any] = {}
    payload: dict[str, Any] = {}
    for assignment in assignments:
        key, sep, value_text = assignment.partition("=")
        if not sep:
            raise ValueError(f"assignment must be KEY=VALUE: {assignment}")
        target = payload
        if key.startswith("command."):
            key = key.removeprefix("command.")
            target = command
        elif key.startswith("payload."):
            key = key.removeprefix("payload.")
        _set_nested(target, key.split("."), _coerce_value(value_text))
    return command, payload


def _field_lines(model_name: str, prefix: str = "") -> list[str]:
    model = MODELS[model_name]
    lines: list[str] = []
    for field in model.fields:
        name = f"{prefix}{field.name}"
        default = field.default
        suffix = ""
        if field.kind == "enum" and field.enum_name:
            values = ", ".join(value.name for value in ENUMS[field.enum_name].values)
            suffix = f" enum={field.enum_name} [{values}]"
        elif field.kind == "model" and field.model_name:
            lines.append(f"{name}: {field.type_name} model")
            lines.extend(_field_lines(field.model_name, prefix=f"{name}."))
            continue
        elif field.kind == "array":
            suffix = f" array_len={field.array_len} element={field.element_type}"
        elif field.kind == "unsupported":
            suffix = " unsupported"
        lines.append(f"{name}: {field.type_name} kind={field.kind} default={default!r}{suffix}")
    return lines


def cmd_list_events(_args: argparse.Namespace) -> int:
    for event in all_events():
        topic = event.topic_name or str(event.topic)
        marker = " renderable" if event.renderable else ""
        print(f"{event.label} topic={topic} model={event.config_model or event.payload_model}{marker}")
    return 0


def cmd_dump_event(args: argparse.Namespace) -> int:
    event = event_by_label(args.event)
    print(f"label: {event.label}")
    print(f"topic: {event.topic_name or event.topic}")
    print(f"payload_model: {event.payload_model}")
    if event.config_model:
        print(f"config_model: {event.config_model}")
    print(f"renderable: {event.renderable}")
    print("command_defaults:")
    print(json.dumps(command_defaults(event), indent=2, sort_keys=True))
    print("payload_defaults:")
    print(json.dumps(payload_defaults(event), indent=2, sort_keys=True))
    print("fields:")
    model_name = event.config_model or event.payload_model
    for line in _field_lines(model_name):
        print(f"  {line}")
    return 0


def cmd_payload_hex(args: argparse.Namespace) -> int:
    event = event_by_label(args.event)
    command_values, payload_values = _split_assignments(args.set)
    payload = payload_bytes_for_event(
        event,
        command_values=command_values,
        payload_values=payload_values,
    )
    print(payload.hex())
    return 0


def cmd_render_json(args: argparse.Namespace) -> int:
    event = event_by_label(args.event)
    command_values, payload_values = _split_assignments(args.set)
    config = renderer_json_for_event(
        event,
        command_values=command_values,
        payload_values=payload_values,
        frames=args.frames,
        mode=args.mode,
    )
    print(json.dumps(config, indent=2, sort_keys=True))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="led-pubsub-viewer")
    sub = parser.add_subparsers(dest="command")

    list_events = sub.add_parser("list-events")
    list_events.set_defaults(func=cmd_list_events)

    dump_event = sub.add_parser("dump-event")
    dump_event.add_argument("event")
    dump_event.set_defaults(func=cmd_dump_event)

    payload_hex = sub.add_parser("payload-hex")
    payload_hex.add_argument("event")
    payload_hex.add_argument("--set", action="append", default=[])
    payload_hex.set_defaults(func=cmd_payload_hex)

    render_json = sub.add_parser("render-json")
    render_json.add_argument("event")
    render_json.add_argument("--set", action="append", default=[])
    render_json.add_argument("--frames", default=None)
    render_json.add_argument("--mode", choices=["pipeline", "animation"], default="pipeline")
    render_json.set_defaults(func=cmd_render_json)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if not hasattr(args, "func"):
        from .app import run_app

        run_app()
        return 0
    try:
        return args.func(args)
    except Exception as exc:
        print(f"led-pubsub-viewer: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
