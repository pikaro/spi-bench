from __future__ import annotations

from typing import Any

from .trace import Trace


def _need_numpy():
    try:
        import numpy as np
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "NumPy is required for trace analysis. Install numpy in the active "
            "environment."
        ) from exc
    return np


def plane_stats(trace: Trace, plane: str = "rgb_final") -> dict[str, Any]:
    np = _need_numpy()
    frames = trace.frames(plane)
    return {
        "plane": plane,
        "shape": tuple(int(v) for v in frames.shape),
        "min": [int(v) for v in frames.reshape(-1, 3).min(axis=0)],
        "max": [int(v) for v in frames.reshape(-1, 3).max(axis=0)],
        "mean": [float(v) for v in frames.reshape(-1, 3).mean(axis=0)],
        "nonzero_pixels": int(np.count_nonzero(frames.max(axis=3))),
    }


def frame_deltas(trace: Trace, plane: str = "rgb_final"):
    np = _need_numpy()
    frames = trace.frames(plane).astype(np.int16, copy=False)
    if frames.shape[0] < 2:
        return np.zeros((0, trace.header.spoke_count, trace.header.ring_count))
    return np.abs(np.diff(frames, axis=0)).max(axis=3)


def detect_flicker(
    trace: Trace,
    plane: str = "rgb_final",
    threshold: int = 80,
    percentile: float = 99.5,
    limit: int = 20,
) -> dict[str, Any]:
    np = _need_numpy()
    deltas = frame_deltas(trace, plane)
    if deltas.size == 0:
        return {"plane": plane, "threshold": threshold, "events": []}

    percentile_threshold = float(np.percentile(deltas, percentile))
    cutoff = max(float(threshold), percentile_threshold)
    positions = np.argwhere(deltas >= cutoff)
    if positions.size == 0:
        return {
            "plane": plane,
            "threshold": cutoff,
            "percentile_threshold": percentile_threshold,
            "events": [],
        }

    scores = deltas[positions[:, 0], positions[:, 1], positions[:, 2]]
    order = np.argsort(scores)[::-1][:limit]
    frames = trace.frames(plane)
    events: list[dict[str, Any]] = []
    for entry in order:
        delta_index, spoke, radial = [int(v) for v in positions[entry]]
        events.append(
            {
                "from_frame": trace.frame_number(delta_index),
                "to_frame": trace.frame_number(delta_index + 1),
                "spoke": spoke,
                "radial": radial,
                "score": int(scores[entry]),
                "before": [int(v) for v in frames[delta_index, spoke, radial]],
                "after": [int(v) for v in frames[delta_index + 1, spoke, radial]],
            }
        )

    return {
        "plane": plane,
        "threshold": cutoff,
        "percentile_threshold": percentile_threshold,
        "max_delta": int(deltas.max()),
        "events": events,
    }


def detect_hue_ping_pong(
    trace: Trace,
    threshold: int = 48,
    value_floor: int = 16,
    limit: int = 20,
) -> dict[str, Any]:
    np = _need_numpy()
    frames = trace.frames("hsv_final").astype(np.int16, copy=False)
    if frames.shape[0] < 3:
        return {"events": []}

    hue = frames[..., 0]
    value = frames[..., 2]
    jump_a = np.abs(hue[1:-1] - hue[:-2])
    jump_b = np.abs(hue[2:] - hue[1:-1])
    returns = np.abs(hue[2:] - hue[:-2])
    bright = (
        (value[:-2] >= value_floor)
        & (value[1:-1] >= value_floor)
        & (value[2:] >= value_floor)
    )
    score = np.minimum(jump_a, jump_b)
    mask = (score >= threshold) & (returns <= 4) & bright
    positions = np.argwhere(mask)
    if positions.size == 0:
        return {"events": []}

    scores = score[positions[:, 0], positions[:, 1], positions[:, 2]]
    order = np.argsort(scores)[::-1][:limit]
    events: list[dict[str, Any]] = []
    for entry in order:
        index, spoke, radial = [int(v) for v in positions[entry]]
        events.append(
            {
                "frames": [
                    trace.frame_number(index),
                    trace.frame_number(index + 1),
                    trace.frame_number(index + 2),
                ],
                "spoke": spoke,
                "radial": radial,
                "score": int(scores[entry]),
                "hues": [
                    int(hue[index, spoke, radial]),
                    int(hue[index + 1, spoke, radial]),
                    int(hue[index + 2, spoke, radial]),
                ],
                "values": [
                    int(value[index, spoke, radial]),
                    int(value[index + 1, spoke, radial]),
                    int(value[index + 2, spoke, radial]),
                ],
            }
        )
    return {"events": events}


def region_stats(trace: Trace, spokes: slice, radials: slice, plane: str):
    np = _need_numpy()
    region = trace.region(spokes, radials, plane).astype(np.uint16, copy=False)
    return {
        "plane": plane,
        "shape": tuple(int(v) for v in region.shape),
        "min": [int(v) for v in region.reshape(-1, 3).min(axis=0)],
        "max": [int(v) for v in region.reshape(-1, 3).max(axis=0)],
        "mean": [float(v) for v in region.reshape(-1, 3).mean(axis=0)],
        "variance": [float(v) for v in region.reshape(-1, 3).var(axis=0)],
        "active_frames": int(np.count_nonzero(region.max(axis=(1, 2, 3)))),
    }
