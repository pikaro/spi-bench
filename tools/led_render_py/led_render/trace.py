from __future__ import annotations

import dataclasses
import json
import mmap
import pathlib
import struct
from typing import Any


HEADER_STRUCT = struct.Struct("<4sHHIIIIIIHHHHIIII10I")
HEADER_SIZE = 96
MAGIC = b"TLED"
VERSION = 1

PLANE_BITS = {
    "hsv_final": 1 << 0,
    "rgb_final": 1 << 1,
    "hsv_scratch": 1 << 2,
}
PLANE_ORDER = tuple(PLANE_BITS)


@dataclasses.dataclass(frozen=True)
class TraceHeader:
    version: int
    header_size: int
    flags: int
    frame_count: int
    first_frame: int
    fps_num: int
    fps_den: int
    frame_step_us: int
    strip_count: int
    segments_per_strip: int
    spoke_count: int
    ring_count: int
    pixel_count: int
    plane_mask: int
    bytes_per_frame: int
    metadata_bytes: int


def _need_numpy():
    try:
        import numpy as np
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "NumPy is required for frame access and analysis. "
            "Install numpy in the active environment."
        ) from exc
    return np


class Trace:
    def __init__(self, path: str | pathlib.Path):
        self.path = pathlib.Path(path)
        self._file = self.path.open("rb")
        self._mmap = mmap.mmap(self._file.fileno(), 0, access=mmap.ACCESS_READ)
        self.header = self._read_header()
        self.metadata = self._read_metadata()

        expected = (
            self.header.header_size
            + self.header.metadata_bytes
            + self.header.frame_count * self.header.bytes_per_frame
        )
        if len(self._mmap) < expected:
            raise ValueError(
                f"{self.path} is truncated: expected at least {expected} bytes, "
                f"got {len(self._mmap)}"
            )

    def close(self) -> None:
        self._mmap.close()
        self._file.close()

    def __enter__(self) -> "Trace":
        return self

    def __exit__(self, *_args: object) -> None:
        self.close()

    @property
    def plane_names(self) -> tuple[str, ...]:
        return tuple(
            name for name in PLANE_ORDER if self.header.plane_mask & PLANE_BITS[name]
        )

    @property
    def fps(self) -> float:
        if self.header.fps_den == 0:
            return 0.0
        return self.header.fps_num / self.header.fps_den

    def frame_number(self, index: int) -> int:
        if index < 0:
            index += self.header.frame_count
        if index < 0 or index >= self.header.frame_count:
            raise IndexError(index)
        return self.header.first_frame + index

    def frames(self, plane: str = "rgb_final"):
        np = _need_numpy()
        plane_index = self._plane_index(plane)
        plane_bytes = self.header.pixel_count * 3
        offset = (
            self.header.header_size
            + self.header.metadata_bytes
            + plane_index * plane_bytes
        )
        shape = (
            self.header.frame_count,
            self.header.spoke_count,
            self.header.ring_count,
            3,
        )
        strides = (
            self.header.bytes_per_frame,
            self.header.ring_count * 3,
            3,
            1,
        )
        return np.ndarray(
            shape=shape,
            dtype=np.uint8,
            buffer=self._mmap,
            offset=offset,
            strides=strides,
        )

    def frame(self, index: int, plane: str = "rgb_final"):
        return self.frames(plane)[index]

    def pixel_series(self, spoke: int, radial: int, plane: str = "rgb_final"):
        return self.frames(plane)[:, spoke, radial, :]

    def region(
        self,
        spokes: slice,
        radials: slice,
        plane: str = "rgb_final",
    ):
        return self.frames(plane)[:, spokes, radials, :]

    def summary_dict(self) -> dict[str, Any]:
        return {
            "path": str(self.path),
            "frames": self.header.frame_count,
            "first_frame": self.header.first_frame,
            "fps": self.fps,
            "frame_step_us": self.header.frame_step_us,
            "spokes": self.header.spoke_count,
            "rings": self.header.ring_count,
            "pixels": self.header.pixel_count,
            "planes": self.plane_names,
            "metadata": self.metadata,
        }

    def _read_header(self) -> TraceHeader:
        if len(self._mmap) < HEADER_SIZE:
            raise ValueError(f"{self.path} is too small to be a TLED trace")
        unpacked = HEADER_STRUCT.unpack_from(self._mmap, 0)
        magic = unpacked[0]
        if magic != MAGIC:
            raise ValueError(f"{self.path} has invalid trace magic {magic!r}")
        version = unpacked[1]
        if version != VERSION:
            raise ValueError(f"{self.path} has unsupported trace version {version}")
        header_size = unpacked[2]
        if header_size != HEADER_SIZE:
            raise ValueError(
                f"{self.path} has unsupported header size {header_size}"
            )
        return TraceHeader(
            version=version,
            header_size=header_size,
            flags=unpacked[3],
            frame_count=unpacked[4],
            first_frame=unpacked[5],
            fps_num=unpacked[6],
            fps_den=unpacked[7],
            frame_step_us=unpacked[8],
            strip_count=unpacked[9],
            segments_per_strip=unpacked[10],
            spoke_count=unpacked[11],
            ring_count=unpacked[12],
            pixel_count=unpacked[13],
            plane_mask=unpacked[14],
            bytes_per_frame=unpacked[15],
            metadata_bytes=unpacked[16],
        )

    def _read_metadata(self) -> dict[str, Any]:
        start = self.header.header_size
        end = start + self.header.metadata_bytes
        if self.header.metadata_bytes == 0:
            return {}
        return json.loads(self._mmap[start:end].decode("utf-8"))

    def _plane_index(self, plane: str) -> int:
        if plane not in PLANE_BITS:
            raise KeyError(f"Unknown plane {plane!r}")
        if not (self.header.plane_mask & PLANE_BITS[plane]):
            raise KeyError(
                f"Plane {plane!r} is not present; available: {self.plane_names}"
            )
        index = 0
        for name in PLANE_ORDER:
            if name == plane:
                return index
            if self.header.plane_mask & PLANE_BITS[name]:
                index += 1
        raise KeyError(plane)
