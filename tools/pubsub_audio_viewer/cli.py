from __future__ import annotations

import argparse
from pathlib import Path

from .viewer import run_viewer


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Render local audio PubSub FFT, peak, and beat events.",
    )
    parser.add_argument(
        "--socket",
        type=Path,
        default=Path("/tmp/totem-pubsub.sock"),
        help="Unix-domain socket exposed by bin/pubsub-udp-peer --local-socket.",
    )
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--height", type=int, default=420)
    parser.add_argument("--fps", type=float, default=60.0)
    parser.add_argument("--connect-timeout", type=float, default=10.0)
    parser.add_argument(
        "--fft-scale",
        type=int,
        default=255,
        help="Scaled FFT value that maps to a full-height bar.",
    )
    parser.add_argument("--peak-hold-ms", type=int, default=180)
    parser.add_argument("--beat-hold-ms", type=int, default=160)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    run_viewer(
        socket_path=args.socket,
        width=args.width,
        height=args.height,
        fps=args.fps,
        connect_timeout_s=args.connect_timeout,
        fft_scale=args.fft_scale,
        peak_hold_ms=args.peak_hold_ms,
        beat_hold_ms=args.beat_hold_ms,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
