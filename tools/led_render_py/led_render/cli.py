from __future__ import annotations

import argparse
import json
import subprocess
import sys

from .analysis import (
    detect_flicker,
    detect_hue_ping_pong,
    plane_stats,
    region_stats,
)
from .trace import Trace
from .runner import run_render
from .viewer import capture, play


def _slice(value: str) -> slice:
    if ":" not in value:
        start = int(value)
        return slice(start, start + 1)
    start_text, end_text = value.split(":", 1)
    start = int(start_text) if start_text else None
    end = int(end_text) if end_text else None
    return slice(start, end)


def _print_json(value) -> None:
    print(json.dumps(value, indent=2, sort_keys=True))


def cmd_summary(args: argparse.Namespace) -> int:
    with Trace(args.trace) as trace:
        summary = trace.summary_dict()
        if args.stats:
            summary["plane_stats"] = [
                plane_stats(trace, plane) for plane in trace.plane_names
            ]
        _print_json(summary)
    return 0


def cmd_flicker(args: argparse.Namespace) -> int:
    with Trace(args.trace) as trace:
        result = detect_flicker(
            trace,
            plane=args.plane,
            threshold=args.threshold,
            percentile=args.percentile,
            limit=args.limit,
        )
        if args.hue_ping_pong and "hsv_final" in trace.plane_names:
            result["hue_ping_pong"] = detect_hue_ping_pong(
                trace,
                threshold=args.hue_threshold,
                value_floor=args.value_floor,
                limit=args.limit,
            )
        _print_json(result)
    return 0


def cmd_pixel(args: argparse.Namespace) -> int:
    with Trace(args.trace) as trace:
        series = trace.pixel_series(args.spoke, args.radial, args.plane)
        rows = [
            {
                "frame": trace.frame_number(index),
                "channels": [int(value) for value in channels],
            }
            for index, channels in enumerate(series)
        ]
        _print_json(rows)
    return 0


def cmd_region(args: argparse.Namespace) -> int:
    with Trace(args.trace) as trace:
        _print_json(
            region_stats(
                trace,
                spokes=_slice(args.spokes),
                radials=_slice(args.radials),
                plane=args.plane,
            )
        )
    return 0


def cmd_capture(args: argparse.Namespace) -> int:
    with Trace(args.trace) as trace:
        capture(
            trace,
            args.output,
            frame=args.frame,
            plane=args.plane,
            scale=args.scale,
            glare=args.glare,
            layout=args.layout,
            spacing=args.spacing,
            show_frame_label=not args.no_frame_label,
        )
    print(args.output)
    return 0


def cmd_view(args: argparse.Namespace) -> int:
    with Trace(args.trace) as trace:
        if args.capture:
            capture(
                trace,
                args.capture,
                frame=args.frame,
                plane=args.plane,
                scale=args.scale,
                glare=args.glare,
                layout=args.layout,
                spacing=args.spacing,
                show_frame_label=not args.no_frame_label,
            )
            print(args.capture)
            return 0
        play(
            trace,
            plane=args.plane,
            fps=args.fps,
            scale=args.scale,
            glare=args.glare,
            layout=args.layout,
            spacing=args.spacing,
            show_frame_label=not args.no_frame_label,
        )
    return 0


def cmd_render(args: argparse.Namespace) -> int:
    try:
        run_render(
            args.config,
            args.output,
            frames=args.frames,
            animation=args.animation,
            fps=args.fps,
            mode=args.mode,
            include_scratch=args.include_scratch,
        )
    except subprocess.CalledProcessError as exc:
        return exc.returncode
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="led-analyze")
    sub = parser.add_subparsers(dest="command", required=True)

    summary = sub.add_parser("summary")
    summary.add_argument("trace")
    summary.add_argument("--stats", action="store_true")
    summary.set_defaults(func=cmd_summary)

    flicker = sub.add_parser("flicker")
    flicker.add_argument("trace")
    flicker.add_argument("--plane", default="rgb_final")
    flicker.add_argument("--threshold", type=int, default=80)
    flicker.add_argument("--percentile", type=float, default=99.5)
    flicker.add_argument("--limit", type=int, default=20)
    flicker.add_argument("--hue-ping-pong", action="store_true")
    flicker.add_argument("--hue-threshold", type=int, default=48)
    flicker.add_argument("--value-floor", type=int, default=16)
    flicker.set_defaults(func=cmd_flicker)

    pixel = sub.add_parser("pixel")
    pixel.add_argument("trace")
    pixel.add_argument("--spoke", type=int, required=True)
    pixel.add_argument("--radial", type=int, required=True)
    pixel.add_argument("--plane", default="rgb_final")
    pixel.set_defaults(func=cmd_pixel)

    region = sub.add_parser("region")
    region.add_argument("trace")
    region.add_argument("--spokes", default=":")
    region.add_argument("--radials", default=":")
    region.add_argument("--plane", default="rgb_final")
    region.set_defaults(func=cmd_region)

    capture_parser = sub.add_parser("capture")
    capture_parser.add_argument("trace")
    capture_parser.add_argument("--output", required=True)
    capture_parser.add_argument("--frame", type=int, default=0)
    capture_parser.add_argument("--plane", default="rgb_final")
    capture_parser.add_argument("--scale", type=int, default=12)
    capture_parser.add_argument("--layout", choices=("heatmap", "radial"), default="heatmap")
    capture_parser.add_argument("--spacing", type=float, default=1.35)
    capture_parser.add_argument("--glare", action="store_true")
    capture_parser.add_argument("--no-frame-label", action="store_true")
    capture_parser.set_defaults(func=cmd_capture)

    view = sub.add_parser("view")
    view.add_argument("trace")
    view.add_argument("--plane", default="rgb_final")
    view.add_argument("--fps", type=float)
    view.add_argument("--scale", type=int, default=12)
    view.add_argument("--layout", choices=("heatmap", "radial"), default="heatmap")
    view.add_argument("--spacing", type=float, default=1.35)
    view.add_argument("--glare", action="store_true")
    view.add_argument("--no-frame-label", action="store_true")
    view.add_argument("--capture")
    view.add_argument("--frame", type=int, default=0)
    view.set_defaults(func=cmd_view)

    render = sub.add_parser("render")
    render.add_argument("--config", required=True)
    render.add_argument("--output", required=True)
    render.add_argument("--frames")
    render.add_argument("--animation")
    render.add_argument("--fps", type=int)
    render.add_argument("--mode", choices=("pipeline", "animation"))
    render.add_argument("--include-scratch", action="store_true")
    render.set_defaults(func=cmd_render)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except Exception as exc:
        print(f"led-analyze: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
