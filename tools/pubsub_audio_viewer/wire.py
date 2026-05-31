from __future__ import annotations

from dataclasses import dataclass
import struct


TOPIC_BEAT = 1 << 3
TOPIC_FFT_FRAME = 1 << 4
TOPIC_BUTTON = 1 << 8
TOPIC_PEAK = 1 << 11

TOPICS = (TOPIC_BEAT, TOPIC_FFT_FRAME, TOPIC_PEAK)

BUTTON_EVENT_PRESSED = 0
PERIPHERAL_BUTTON_CALIBRATION = 1

BAND_NAMES = (
    "Sub",
    "Bass",
    "LowMid",
    "Mid",
    "HighMid",
    "Presence",
    "Brill",
    "Air",
)

PEAK_GROUP_NAMES = {
    0: "Bass",
    1: "Mid",
    2: "High",
}

BEAT_KIND_NAMES = {
    0: "ExpectedHit",
    1: "ExpectedMiss",
    2: "Reacquired",
    3: "Lost",
}


@dataclass(frozen=True)
class FftFrame:
    bands: tuple[int, ...]


@dataclass(frozen=True)
class BeatEvent:
    kind: int
    bpm: int
    confidence: int
    energy: int
    sequence: int

    @property
    def kind_name(self) -> str:
        return BEAT_KIND_NAMES.get(self.kind, f"Unknown({self.kind})")


@dataclass(frozen=True)
class PeakEvent:
    group: int
    energy: int
    lower_band: int
    upper_band: int
    frame_sequence: int

    @property
    def group_name(self) -> str:
        return PEAK_GROUP_NAMES.get(self.group, f"Unknown({self.group})")


def decode_payload_hex(payload_hex: str | None) -> bytes:
    if payload_hex is None:
        raise ValueError("missing payload_hex")
    return bytes.fromhex(payload_hex)


def decode_fft_frame(payload: bytes) -> FftFrame:
    expected = struct.calcsize("<8H")
    if len(payload) != expected:
        raise ValueError(f"FFT frame payload is {len(payload)} bytes, expected {expected}")
    return FftFrame(bands=struct.unpack("<8H", payload))


def decode_beat_event(payload: bytes) -> BeatEvent:
    expected = struct.calcsize("<BBBBI")
    if len(payload) != expected:
        raise ValueError(f"beat payload is {len(payload)} bytes, expected {expected}")
    kind, bpm, confidence, energy, sequence = struct.unpack("<BBBBI", payload)
    return BeatEvent(
        kind=kind,
        bpm=bpm,
        confidence=confidence,
        energy=energy,
        sequence=sequence,
    )


def decode_peak_event(payload: bytes) -> PeakEvent:
    expected = struct.calcsize("<BBBBI")
    if len(payload) != expected:
        raise ValueError(f"peak payload is {len(payload)} bytes, expected {expected}")
    group, energy, lower_band, upper_band, frame_sequence = struct.unpack(
        "<BBBBI",
        payload,
    )
    return PeakEvent(
        group=group,
        energy=energy,
        lower_band=lower_band,
        upper_band=upper_band,
        frame_sequence=frame_sequence,
    )


def encode_calibration_button_pressed() -> bytes:
    return struct.pack(
        "<BB",
        BUTTON_EVENT_PRESSED,
        PERIPHERAL_BUTTON_CALIBRATION,
    )
