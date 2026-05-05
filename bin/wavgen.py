#!/usr/bin/env python3
from __future__ import annotations

import math
import wave
from argparse import ArgumentParser
from dataclasses import dataclass, field
from typing import Callable
from pathlib import Path

import numpy as np

parser = ArgumentParser(description="Generate small WAV fixtures for media FFT tests")
parser.add_argument("output", nargs="?", default="test.wav")
parser.add_argument("preset", nargs="?", default="beat",
                    choices=("beat", "tone", "bands", "mixed"))
parser.add_argument("--sample-rate", type=int, default=16_000)
parser.add_argument("--duration-ms", type=int, default=12_000)
parser.add_argument("--bpm", type=float, default=100.0)
parser.add_argument("--freq", type=float, default=100.0)
args = parser.parse_args()

OUTPUT_PATH = Path("data/media/littlefs") / args.output
OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)


# --------- Core utils ---------


def db_to_lin(db: float) -> float:
    return 10 ** (db / 20.0)


def lin_to_db(x: float) -> float:
    return 20 * math.log10(max(x, 1e-12))


def hann_fade(n: int) -> np.ndarray:
    """Half-Hann (cosine) fade of length n."""
    if n <= 1:
        return np.ones(n)
    w = 0.5 * (1 - np.cos(np.linspace(0, math.pi, n)))
    return w


def apply_fades(x: np.ndarray, fade_samps: int) -> np.ndarray:
    if fade_samps <= 0:
        return x
    fade = hann_fade(fade_samps)
    y = x.copy()
    y[:fade_samps] *= fade
    y[-fade_samps:] *= fade[::-1]
    return y


def ensure_len(x: np.ndarray, n: int) -> np.ndarray:
    if len(x) == n:
        return x
    if len(x) > n:
        return x[:n]
    y = np.zeros(n, dtype=np.float64)
    y[: len(x)] = x
    return y


# --------- Signal primitives ---------


def sine(freq: float, dur_s: float, fs: int, phase_rad: float = 0.0) -> np.ndarray:
    t = np.arange(int(round(dur_s * fs))) / fs
    return np.sin(2 * math.pi * freq * t + phase_rad)


def linear_sweep(
    f0: float, f1: float, dur_s: float, fs: int, phase0_rad: float = 0.0
) -> np.ndarray:
    n = int(round(dur_s * fs))
    t = np.arange(n) / fs
    k = (f1 - f0) / dur_s  # Hz/s
    phase = 2 * math.pi * (f0 * t + 0.5 * k * t**2) + phase0_rad
    return np.sin(phase)


def beats(
    carrier: Callable[[float], np.ndarray],
    period_ms: float,
    duty: float,
    dur_s: float,
    fs: int,
    edge_ms: float = 5.0,
) -> np.ndarray:
    """
    Periodic bursts of 'carrier' with given duty. 'carrier' must accept segment duration seconds.
    """
    n = int(round(dur_s * fs))
    y = np.zeros(n, dtype=np.float64)
    period = int(round(period_ms * fs / 1000.0))
    on = int(round(period * duty))
    # minimum 2*fade
    fade = int(round(edge_ms * fs / 1000.0))
    on = max(on, max(1, 2 * fade + 1))
    i = 0
    while i < n:
        on_end = min(i + on, n)
        seg = carrier((on_end - i) / fs)
        seg = apply_fades(seg, min(fade, len(seg) // 2))
        y[i:on_end] += ensure_len(seg, on_end - i)
        i += period if period > 0 else on
    return y


def bandlimited_noise(f_lo: float, f_hi: float, dur_s: float, fs: int) -> np.ndarray:
    """Simple brick-wall in frequency domain using rFFT/iFFT."""
    n = int(round(dur_s * fs))
    nfft = int(2 ** math.ceil(math.log2(max(8, n))))
    noise = np.random.randn(nfft)
    spec = np.fft.rfft(noise)
    freqs = np.fft.rfftfreq(nfft, d=1 / fs)
    mask = (freqs >= f_lo) & (freqs <= f_hi)
    spec *= mask
    x = np.fft.irfft(spec, nfft)
    x = x[:n]
    # Normalize to RMS ~ -12 dBFS baseline
    rms = np.sqrt(np.mean(x**2)) + 1e-12
    x = x / rms * db_to_lin(-12)
    return x


# --------- Timeline builder ---------


@dataclass
class Segment:
    dur_ms: int
    _layers: list[np.ndarray] = field(default_factory=list)

    def add(self, x: np.ndarray) -> Segment:
        self._layers.append(x.astype(np.float64))
        return self

    @property
    def dur_s(self) -> float:
        return self.dur_ms / 1000.0


@dataclass
class Timeline:
    fs: int = 48_000
    fade_ms: int = 8  # per-segment endcaps
    headroom_db: float = 3.0  # post-mix normalize target peak ~ -3 dBFS
    _segments: list[Segment] = field(default_factory=list)

    # --- building ---

    def segment(self, dur_ms: int) -> Segment:
        seg = Segment(dur_ms)
        self._segments.append(seg)
        return seg

    # --- primitives for a segment ---

    def tone_beats(
        self,
        seg: Segment,
        freq: float,
        vol: float = 1.0,
        period_ms: int = 200,
        duty: float = 0.35,
        phase_deg: float = 0.0,
    ) -> Timeline:
        def carrier(d: float) -> np.ndarray:
            return vol * sine(freq, d, self.fs, math.radians(phase_deg))

        seg.add(beats(carrier, period_ms, duty, seg.dur_s, self.fs))
        return self

    def multi_tone_beats(
        self,
        seg: Segment,
        freqs: list[float],
        vols: list[float] | None = None,
        period_ms: int = 200,
        duty: float = 0.35,
        phases_deg: list[float] | None = None,
    ) -> Timeline:
        vols = vols or [1.0] * len(freqs)
        phases_deg = phases_deg or [0.0] * len(freqs)
        layer = np.zeros(int(round(seg.dur_s * self.fs)))
        for f, v, p in zip(freqs, vols, phases_deg, strict=False):

            def carrier(d: float, f=f, v=v, p=p) -> np.ndarray:
                return v * sine(f, d, self.fs, math.radians(p))

            layer += beats(carrier, period_ms, duty, seg.dur_s, self.fs)
        seg.add(layer)
        return self

    def band_noise_beats(
        self,
        seg: Segment,
        f_lo: float,
        f_hi: float,
        vol: float = 1.0,
        period_ms: int = 250,
        duty: float = 0.5,
    ) -> Timeline:
        def carrier(d: float) -> np.ndarray:
            return vol * bandlimited_noise(f_lo, f_hi, d, self.fs)

        seg.add(beats(carrier, period_ms, duty, seg.dur_s, self.fs))
        return self

    def sweep(
        self,
        seg: Segment,
        f0: float,
        f1: float,
        vol: float = 1.0,
        phase_deg: float = 0.0,
    ) -> Timeline:
        x = vol * linear_sweep(f0, f1, seg.dur_s, self.fs, math.radians(phase_deg))
        seg.add(x)
        return self

    def dual_sweeps(
        self,
        seg: Segment,
        f0_a: float,
        f1_a: float,
        f0_b: float,
        f1_b: float,
        vol_a: float = 1.0,
        vol_b: float = 1.0,
        phase_a_deg: float = 0.0,
        phase_b_deg: float = 0.0,
    ) -> Timeline:
        a = vol_a * linear_sweep(
            f0_a, f1_a, seg.dur_s, self.fs, math.radians(phase_a_deg)
        )
        b = vol_b * linear_sweep(
            f0_b, f1_b, seg.dur_s, self.fs, math.radians(phase_b_deg)
        )
        seg.add(a + b)
        return self

    # --- render ---

    def render(self, normalize: bool = True) -> np.ndarray:
        blocks: list[np.ndarray] = []
        fade_samps = int(round(self.fade_ms * self.fs / 1000.0))
        for seg in self._segments:
            n = int(round(seg.dur_s * self.fs))
            if not seg._layers:
                blocks.append(np.zeros(n))
                continue
            mix = np.zeros(n, dtype=np.float64)
            for layer in seg._layers:
                mix += ensure_len(layer, n)
            mix = apply_fades(mix, min(fade_samps, n // 2))
            blocks.append(mix)
        y = np.concatenate(blocks) if blocks else np.zeros(0, dtype=np.float64)

        # Headroom / normalization
        peak = np.max(np.abs(y)) + 1e-12
        if normalize and peak > 0:
            target = db_to_lin(-self.headroom_db)
            y = y / peak * target
        # Clip guard
        y = np.clip(y, -1.0, 1.0)
        return y.astype(np.float32)

    def write_wav(self, path: Path, normalize: bool = True, pcm_bits: int = 16) -> None:
        y = self.render(normalize=normalize)
        if pcm_bits == 16:
            pcm = np.int16(np.clip(y, -1, 1) * 32767.0)
            with wave.open(path.as_posix(), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(self.fs)
                w.writeframes(pcm.tobytes())
        elif pcm_bits == 24:
            scaled = np.int32(np.clip(y, -1, 1) * 8388607.0)
            b = (scaled & 0xFFFFFF).astype(np.uint32)
            bytes24 = (
                np.column_stack([(b >> 0) & 0xFF, (b >> 8) & 0xFF, (b >> 16) & 0xFF])
                .astype(np.uint8)
                .tobytes()
            )
            with wave.open(path.as_posix(), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(3)
                w.setframerate(self.fs)
                w.writeframes(bytes24)
        else:
            raise ValueError("pcm_bits must be 16 or 24")


# Utility: silence
def add_timeline_silence(tl: Timeline, ms: int):
    seg = tl.segment(ms)
    return seg


def test_mixed(tl: Timeline):
    # 1. Silence
    add_timeline_silence(tl, 2000)

    # 2. Impulse
    seg = tl.segment(500)
    x = np.zeros(int(seg.dur_s * tl.fs))
    x[0] = 1.0  # unit impulse
    seg.add(apply_fades(x, 0))

    add_timeline_silence(tl, 200)

    # 3. Step ladder at 50 Hz
    seg = tl.segment(3000)
    levels_db = [-36, -30, -24, -18]  # visible but safe
    for i, db in enumerate(levels_db):
        tl.tone_beats(seg, 50, vol=db_to_lin(db), period_ms=500, duty=0.5, phase_deg=0)

    add_timeline_silence(tl, 200)

    # 4–9. Single beats per band
    for f in [120, 400, 1000, 2500, 5000, 12000]:
        seg = tl.segment(3000)
        tl.tone_beats(seg, f, vol=db_to_lin(-18), period_ms=500, duty=0.5)
        add_timeline_silence(tl, 200)

    # 10. Broadband noise bursts
    seg = tl.segment(5000)
    tl.band_noise_beats(seg, 100, 10000, vol=db_to_lin(-12), period_ms=500, duty=0.5)
    add_timeline_silence(tl, 200)

    # 11. Upward sweep
    seg = tl.segment(6000)
    tl.sweep(seg, 20, 15000, vol=db_to_lin(-12))
    add_timeline_silence(tl, 200)

    # 12. Downward sweep
    seg = tl.segment(6000)
    tl.sweep(seg, 15000, 20, vol=db_to_lin(-12))
    add_timeline_silence(tl, 200)

    # 13. Dual-tone IMD (60 Hz + 7 kHz)
    seg = tl.segment(4000)
    tl.multi_tone_beats(
        seg, [60, 7000], vols=[db_to_lin(-18), db_to_lin(-12)], period_ms=500, duty=1.0
    )
    add_timeline_silence(tl, 200)

    # 14. Multitone (log spaced 15 tones)
    freqs = np.geomspace(100, 8000, 15)
    seg = tl.segment(5000)
    tl.multi_tone_beats(
        seg, freqs.tolist(), vols=[db_to_lin(-24)] * len(freqs), period_ms=500, duty=1.0
    )
    add_timeline_silence(tl, 200)

    # 15. Opposite sweeps
    seg = tl.segment(8000)
    tl.dual_sweeps(
        seg, 1000, 6000, 6000, 1000, vol_a=db_to_lin(-18), vol_b=db_to_lin(-18)
    )


def test_beat_bands(tl: Timeline, segment_ms: int = 1500):
    for f in [120, 400, 1000, 2500, 5000, 12000]:
        seg = tl.segment(segment_ms)
        tl.tone_beats(seg, f, vol=db_to_lin(-18), period_ms=500, duty=0.5)
        add_timeline_silence(tl, 200)


def test_beat(tl: Timeline, duration_ms: int, bpm: float, freq: float):
    seg = tl.segment(duration_ms)
    period_ms = int(round(60_000.0 / bpm))
    tl.tone_beats(seg, freq, vol=db_to_lin(-18), period_ms=period_ms, duty=0.2)


def test_tone(tl: Timeline, duration_ms: int, freq: float):
    seg = tl.segment(duration_ms)
    seg.add(db_to_lin(-18) * sine(freq, seg.dur_s, tl.fs))


# --------- Example usage ---------
if __name__ == "__main__":
    tl = Timeline(fs=args.sample_rate, fade_ms=8, headroom_db=3.0)

    if args.preset == "mixed":
        test_mixed(tl)
    elif args.preset == "bands":
        test_beat_bands(tl)
    elif args.preset == "beat":
        test_beat(tl, args.duration_ms, args.bpm, args.freq)
    elif args.preset == "tone":
        test_tone(tl, args.duration_ms, args.freq)
    else:
        raise ValueError(f"Unknown preset: {args.preset}")

    tl.write_wav(OUTPUT_PATH, normalize=True, pcm_bits=16)
    size = OUTPUT_PATH.stat().st_size
    print(
        f"Wrote {OUTPUT_PATH} ({size} bytes, preset={args.preset}, "
        f"fs={args.sample_rate}Hz)"
    )
