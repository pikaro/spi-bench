from __future__ import annotations

from dataclasses import dataclass, field
import os
import time
from pathlib import Path
from typing import Any

from .client import LocalPubSubClient
from .wire import (
    BAND_NAMES,
    PEAK_GROUP_NAMES,
    TOPIC_BEAT,
    TOPIC_BUTTON,
    TOPIC_FFT_FRAME,
    TOPIC_PEAK,
    TOPICS,
    BeatEvent,
    FftFrame,
    PeakEvent,
    decode_beat_event,
    decode_fft_frame,
    decode_payload_hex,
    decode_peak_event,
    encode_calibration_button_pressed,
)


@dataclass
class Flash:
    until_s: float = 0.0
    energy: int = 0


@dataclass
class PeakFlash:
    event: PeakEvent
    until_s: float


@dataclass
class AudioViewState:
    fft: FftFrame = field(default_factory=lambda: FftFrame((0,) * len(BAND_NAMES)))
    fft_time_s: float = 0.0
    beat: BeatEvent | None = None
    beat_time_s: float = 0.0
    beat_flash: Flash = field(default_factory=Flash)
    peaks: list[PeakFlash] = field(default_factory=list)
    group_flashes: dict[int, Flash] = field(
        default_factory=lambda: {
            group: Flash() for group in PEAK_GROUP_NAMES
        },
    )
    fft_frames: int = 0
    peak_events: int = 0
    beat_events: int = 0
    local_events: int = 0
    calibration_requests: int = 0
    calibration_request_time_s: float = 0.0
    decode_errors: int = 0
    last_error: str | None = None


def _desktop_size(pygame) -> tuple[int, int] | None:
    try:
        sizes = pygame.display.get_desktop_sizes()
    except (AttributeError, pygame.error):
        sizes = []
    if sizes:
        width, height = sizes[0]
        if width > 0 and height > 0:
            return int(width), int(height)

    try:
        info = pygame.display.Info()
    except pygame.error:
        return None
    width = int(getattr(info, "current_w", 0))
    height = int(getattr(info, "current_h", 0))
    if width > 0 and height > 0:
        return width, height
    return None


def _initial_window_size(
    requested_size: tuple[int, int],
    desktop_size: tuple[int, int] | None,
) -> tuple[int, int]:
    requested_width = max(1, requested_size[0])
    requested_height = max(1, requested_size[1])
    if desktop_size is None:
        return requested_width, requested_height

    desktop_width, desktop_height = desktop_size
    max_width = max(320, int(desktop_width * 0.90))
    max_height = max(240, int(desktop_height * 0.86))
    scale = min(
        1.0,
        max_width / requested_width,
        max_height / requested_height,
    )
    return (
        max(1, int(requested_width * scale)),
        max(1, int(requested_height * scale)),
    )


def run_viewer(
    *,
    socket_path: Path,
    width: int,
    height: int,
    fps: float,
    connect_timeout_s: float,
    fft_scale: int,
    peak_hold_ms: int,
    beat_hold_ms: int,
) -> None:
    os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")
    os.environ.setdefault("SDL_VIDEO_MAC_FULLSCREEN_SPACES", "1")
    try:
        import pygame
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "pubsub-audio-view requires pygame in the Python environment used "
            "by bin/pubsub-audio-view."
        ) from exc

    client = LocalPubSubClient(socket_path, TOPICS)
    client.connect(connect_timeout_s)

    pygame.init()
    try:
        window_flags = pygame.RESIZABLE
        screen = pygame.display.set_mode(
            _initial_window_size((width, height), _desktop_size(pygame)),
            window_flags,
        )
        pygame.display.set_caption(f"Totem Audio PubSub - {socket_path}")
        clock = pygame.time.Clock()
        font = pygame.font.SysFont("Menlo", 15) or pygame.font.Font(None, 15)
        small_font = pygame.font.SysFont("Menlo", 12) or pygame.font.Font(None, 12)
        state = AudioViewState()
        running = True
        while running:
            now_s = time.monotonic()
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                elif event.type == pygame.KEYDOWN:
                    if event.key in (pygame.K_ESCAPE, pygame.K_q):
                        running = False
                    elif event.key == pygame.K_c:
                        client.publish(
                            topic=TOPIC_BUTTON,
                            payload=encode_calibration_button_pressed(),
                        )
                        state.calibration_requests += 1
                        state.calibration_request_time_s = now_s
                elif event.type == pygame.VIDEORESIZE:
                    screen = pygame.display.set_mode(
                        (event.w, event.h),
                        window_flags,
                    )

            for event in client.poll():
                _apply_event(
                    state,
                    event,
                    now_s,
                    peak_hold_s=peak_hold_ms / 1000.0,
                    beat_hold_s=beat_hold_ms / 1000.0,
                )

            _draw(
                pygame=pygame,
                screen=screen,
                state=state,
                now_s=now_s,
                font=font,
                small_font=small_font,
                fft_scale=fft_scale,
                socket_path=socket_path,
            )
            pygame.display.flip()
            clock.tick(fps)
    finally:
        client.close()
        pygame.quit()


def _apply_event(
    state: AudioViewState,
    event: dict[str, Any],
    now_s: float,
    *,
    peak_hold_s: float,
    beat_hold_s: float,
) -> None:
    if event.get("kind") == "local":
        state.local_events += 1
        return
    if event.get("kind") != "pubsub":
        return

    header = event.get("header")
    if not isinstance(header, dict):
        return
    topic = header.get("topic")
    try:
        payload = decode_payload_hex(event.get("payload_hex"))
        if topic == TOPIC_FFT_FRAME:
            state.fft = decode_fft_frame(payload)
            state.fft_time_s = now_s
            state.fft_frames += 1
        elif topic == TOPIC_PEAK:
            peak = decode_peak_event(payload)
            state.peak_events += 1
            state.peaks.append(PeakFlash(peak, now_s + peak_hold_s))
            state.group_flashes[peak.group] = Flash(now_s + peak_hold_s, peak.energy)
        elif topic == TOPIC_BEAT:
            beat = decode_beat_event(payload)
            state.beat = beat
            state.beat_time_s = now_s
            state.beat_events += 1
            state.beat_flash = Flash(now_s + beat_hold_s, beat.energy)
    except (TypeError, ValueError) as exc:
        state.decode_errors += 1
        state.last_error = str(exc)


def _draw(
    *,
    pygame: Any,
    screen: Any,
    state: AudioViewState,
    now_s: float,
    font: Any,
    small_font: Any,
    fft_scale: int,
    socket_path: Path,
) -> None:
    background = (12, 14, 16)
    text = (226, 231, 235)
    muted = (126, 138, 148)
    chart_bg = (22, 25, 29)
    grid = (43, 49, 56)
    bar = (74, 163, 184)

    width, height = screen.get_size()
    screen.fill(background)
    header_h = 58
    footer_h = 54
    left = 22
    right = 22
    top = header_h + 12
    bottom = height - footer_h - 18
    chart = pygame.Rect(left, top, width - left - right, max(80, bottom - top))

    _draw_text(screen, font, "Audio PubSub", (left, 14), text)
    beat = state.beat
    beat_text = "beat: none"
    if beat is not None:
        beat_age_ms = int((now_s - state.beat_time_s) * 1000)
        beat_text = (
            f"beat: {beat.kind_name} bpm={beat.bpm} conf={beat.confidence} "
            f"energy={beat.energy} seq={beat.sequence} age={beat_age_ms}ms"
        )
    _draw_text(screen, small_font, beat_text, (left, 37), text)
    counters = (
        f"fft={state.fft_frames} peaks={state.peak_events} "
        f"beats={state.beat_events} local={state.local_events} "
        f"cal={state.calibration_requests} errors={state.decode_errors}"
    )
    _draw_text(screen, small_font, counters, (width - 360, 16), muted)
    _draw_text(screen, small_font, str(socket_path), (width - 360, 37), muted)

    pygame.draw.rect(screen, chart_bg, chart)
    for i in range(1, 4):
        y = chart.top + int(chart.height * i / 4)
        pygame.draw.line(screen, grid, (chart.left, y), (chart.right, y), 1)

    bar_gap = 10
    band_count = len(BAND_NAMES)
    band_w = max(8, (chart.width - (band_count - 1) * bar_gap) // band_count)
    x0 = chart.left + max(0, (chart.width - (band_w * band_count + bar_gap * (band_count - 1))) // 2)
    peak_bar_h = 8
    beat_bar_h = 16
    band_bottom = chart.bottom - beat_bar_h - 8
    band_top = chart.top + peak_bar_h + 8
    band_h = max(1, band_bottom - band_top)

    for index, value in enumerate(state.fft.bands):
        x = x0 + index * (band_w + bar_gap)
        ratio = min(1.0, max(0.0, float(value) / max(1, fft_scale)))
        h = max(2, int(band_h * ratio))
        rect = pygame.Rect(x, band_bottom - h, band_w, h)
        pygame.draw.rect(screen, bar, rect, border_radius=3)
        _draw_text(screen, small_font, str(value), (x, band_bottom - h - 16), muted)
        _draw_text(screen, small_font, BAND_NAMES[index], (x, band_bottom + 6), text)

    state.peaks = [peak for peak in state.peaks if peak.until_s > now_s]
    for peak in state.peaks:
        _draw_peak_bar(
            pygame,
            screen,
            peak.event,
            now_s,
            peak.until_s,
            x0,
            band_w,
            bar_gap,
            chart.top + 4,
            peak_bar_h,
        )

    _draw_peak_status(
        pygame,
        screen,
        state,
        now_s,
        small_font,
        left,
        height - footer_h + 8,
    )
    if state.calibration_requests:
        age_ms = int((now_s - state.calibration_request_time_s) * 1000)
        _draw_text(
            screen,
            small_font,
            f"last calibration request: {age_ms}ms ago",
            (left + 390, height - footer_h + 14),
            (126, 138, 148),
        )
    _draw_beat_bar(pygame, screen, state, now_s, chart.left, chart.right, chart.bottom - beat_bar_h, beat_bar_h)
    if state.last_error is not None:
        _draw_text(screen, small_font, state.last_error, (left, height - 18), (228, 104, 94))


def _draw_peak_bar(
    pygame: Any,
    screen: Any,
    peak: PeakEvent,
    now_s: float,
    until_s: float,
    x0: int,
    band_w: int,
    bar_gap: int,
    y: int,
    height: int,
) -> None:
    colors = {
        0: (229, 187, 74),
        1: (186, 111, 210),
        2: (93, 190, 137),
    }
    lower = max(0, min(len(BAND_NAMES) - 1, peak.lower_band))
    upper = max(lower, min(len(BAND_NAMES) - 1, peak.upper_band))
    x = x0 + lower * (band_w + bar_gap)
    end_x = x0 + upper * (band_w + bar_gap) + band_w
    alpha = max(0.15, min(1.0, (until_s - now_s) / 0.18))
    color = tuple(int(channel * alpha) for channel in colors.get(peak.group, (230, 230, 230)))
    pygame.draw.rect(screen, color, pygame.Rect(x, y, end_x - x, height), border_radius=3)


def _draw_peak_status(
    pygame: Any,
    screen: Any,
    state: AudioViewState,
    now_s: float,
    font: Any,
    x: int,
    y: int,
) -> None:
    colors = {
        0: (229, 187, 74),
        1: (186, 111, 210),
        2: (93, 190, 137),
    }
    muted = (73, 80, 87)
    for group, name in PEAK_GROUP_NAMES.items():
        flash = state.group_flashes[group]
        active = flash.until_s > now_s
        rect = pygame.Rect(x + group * 126, y, 112, 24)
        color = colors[group] if active else muted
        pygame.draw.rect(screen, color, rect, border_radius=4)
        label = f"{name} {flash.energy if active else 0}"
        _draw_text(screen, font, label, (rect.x + 8, rect.y + 5), (15, 17, 20))


def _draw_beat_bar(
    pygame: Any,
    screen: Any,
    state: AudioViewState,
    now_s: float,
    left: int,
    right: int,
    y: int,
    height: int,
) -> None:
    base = (42, 46, 52)
    colors = {
        0: (89, 214, 141),
        1: (237, 150, 79),
        2: (88, 196, 221),
        3: (221, 84, 84),
    }
    pygame.draw.rect(screen, base, pygame.Rect(left, y, right - left, height), border_radius=5)
    beat = state.beat
    if beat is None or state.beat_flash.until_s <= now_s:
        return
    ratio = max(0.2, min(1.0, state.beat_flash.energy / 255.0))
    color = colors.get(beat.kind, (230, 230, 230))
    bar_h = max(4, int(height * ratio))
    pygame.draw.rect(
        screen,
        color,
        pygame.Rect(left, y + height - bar_h, right - left, bar_h),
        border_radius=5,
    )


def _draw_text(screen: Any, font: Any, value: str, pos: tuple[int, int], color: tuple[int, int, int]) -> None:
    surface = font.render(value, True, color)
    screen.blit(surface, pos)
