from __future__ import annotations

import math
import os
import pathlib
from dataclasses import dataclass
from functools import lru_cache

from .trace import Trace


def _need_numpy():
    try:
        import numpy as np
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "NumPy is required for trace viewing. Install numpy in the active "
            "environment."
        ) from exc
    return np


def _blur_frame(frame):
    np = _need_numpy()
    accum = frame.astype(np.uint16)
    count = np.ones(frame.shape[:2], dtype=np.uint16)
    for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, 1)):
        src_y = slice(max(0, -dy), frame.shape[0] - max(0, dy))
        dst_y = slice(max(0, dy), frame.shape[0] - max(0, -dy))
        src_x = slice(max(0, -dx), frame.shape[1] - max(0, dx))
        dst_x = slice(max(0, dx), frame.shape[1] - max(0, -dx))
        accum[dst_y, dst_x] += frame[src_y, src_x].astype(np.uint16)
        count[dst_y, dst_x] += 1
    return (accum // count[..., None]).astype(np.uint8)


FONT_3X5 = {
    "0": ("111", "101", "101", "101", "111"),
    "1": ("010", "110", "010", "010", "111"),
    "2": ("111", "001", "111", "100", "111"),
    "3": ("111", "001", "111", "001", "111"),
    "4": ("101", "101", "111", "001", "001"),
    "5": ("111", "100", "111", "001", "111"),
    "6": ("111", "100", "111", "101", "111"),
    "7": ("111", "001", "010", "010", "010"),
    "8": ("111", "101", "111", "101", "111"),
    "9": ("111", "101", "111", "001", "111"),
    "/": ("001", "001", "010", "100", "100"),
    ".": ("000", "000", "000", "000", "010"),
    "F": ("111", "100", "110", "100", "100"),
    "M": ("101", "111", "111", "101", "101"),
    "S": ("111", "100", "111", "001", "111"),
    "T": ("111", "010", "010", "010", "010"),
    " ": ("000", "000", "000", "000", "000"),
}


def frame_label(trace: Trace, frame: int) -> str:
    source_frame = trace.frame_number(frame)
    last_frame = trace.frame_number(trace.header.frame_count - 1)
    timestamp_ms = int(round((source_frame * trace.header.frame_step_us) / 1000.0))
    return f"F{source_frame}/{last_frame} T{timestamp_ms}MS"


def _draw_frame_label(image, text: str):
    np = _need_numpy()
    text = text.upper()
    scale = max(2, image.shape[1] // 260)
    glyph_width = 3 * scale
    glyph_height = 5 * scale
    gap = scale
    margin = 4 * scale
    width = (len(text) * glyph_width) + max(0, len(text) - 1) * gap
    height = glyph_height
    panel_width = min(image.shape[1], width + (2 * margin))
    panel_height = min(image.shape[0], height + (2 * margin))

    image[:panel_height, :panel_width] = (
        image[:panel_height, :panel_width].astype(np.uint16) // 5
    ).astype(np.uint8)

    cursor_x = margin
    baseline_y = margin
    color = np.array([235, 235, 235], dtype=np.uint8)
    for char in text:
        glyph = FONT_3X5.get(char, FONT_3X5[" "])
        for row, bits in enumerate(glyph):
            for column, bit in enumerate(bits):
                if bit != "1":
                    continue
                y0 = baseline_y + (row * scale)
                x0 = cursor_x + (column * scale)
                image[y0 : y0 + scale, x0 : x0 + scale] = color
        cursor_x += glyph_width + gap
        if cursor_x >= image.shape[1] - margin:
            break
    return image


def _add_frame_label(image, text: str):
    np = _need_numpy()
    scale = max(2, image.shape[1] // 260)
    header_height = (5 * scale) + (8 * scale)
    out = np.zeros((image.shape[0] + header_height, image.shape[1], 3), dtype=np.uint8)
    out[header_height:, :, :] = image
    _draw_frame_label(out, text)
    return out


def _apply_heatmap_glare(source):
    np = _need_numpy()
    glow = _blur_frame(source).astype(np.uint16)
    return np.clip(source.astype(np.uint16) + (glow // 2), 0, 255).astype(np.uint8)


def _apply_preview_brightness(source, brightness: int):
    np = _need_numpy()
    brightness = max(0, min(255, int(brightness)))
    if brightness == 255:
        return source
    return (
        (source.astype(np.uint16) * brightness + 127) // 255
    ).astype(np.uint8)


def _smoothstep(edge0, edge1, value):
    np = _need_numpy()
    if edge1 <= edge0:
        return np.where(value < edge0, 0.0, 1.0)
    x = np.clip((value - edge0) / (edge1 - edge0), 0.0, 1.0)
    return x * x * (3.0 - (2.0 * x))


def _heatmap_frame(source, scale: int, glare: bool):
    np = _need_numpy()
    if glare:
        source = _apply_heatmap_glare(source)
    return np.repeat(np.repeat(source, scale, axis=0), scale, axis=1)


@dataclass(frozen=True)
class _RadialLayout:
    image_size: int
    core_radius: float
    glow_max_radius: float
    glow_opacity: float
    centers: tuple[tuple[int, int], ...]


@lru_cache(maxsize=32)
def _radial_layout(
    spokes: int,
    rings: int,
    inner_radius_centi_mm: int,
    strip_length_centi_mm: int,
    led_size: int,
    spacing_milli: int,
) -> _RadialLayout:
    inner_radius_mm = max(0.0, inner_radius_centi_mm / 100.0)
    strip_length_mm = max(0.01, strip_length_centi_mm / 100.0)
    spacing = max(0.25, spacing_milli / 1000.0)
    core_radius = max(1.0, led_size / 2.0)

    image_size = max(
        320,
        int(math.ceil(1000.0 * (core_radius / 5.0) * (spacing / 1.35))),
    )
    center = image_size / 2.0

    apothem = (image_size / 2.0) * 0.92 * math.cos(math.pi / max(3, spokes))
    outer_radius = apothem * 0.95
    inner_ratio = inner_radius_mm / (inner_radius_mm + strip_length_mm)
    inner_radius = outer_radius * inner_ratio
    strip_radius = outer_radius - inner_radius

    centers = []
    step = (2.0 * math.pi) / spokes
    for spoke in range(spokes):
        angle = (-math.pi / 2.0) + ((spoke + 0.5) * step)
        angle_cos = math.cos(angle)
        angle_sin = math.sin(angle)
        for radial in range(rings):
            if rings <= 1:
                t = 0.0
            else:
                t = radial / (rings - 1)
            radius = inner_radius + (strip_radius * t)
            x = int(round(center + (angle_cos * radius)))
            y = int(round(center + (angle_sin * radius)))
            centers.append((x, y))

    return _RadialLayout(
        image_size=image_size,
        core_radius=core_radius,
        glow_max_radius=core_radius * 20.0,
        glow_opacity=0.70,
        centers=tuple(centers),
    )


def _kernel_grid(radius: int):
    np = _need_numpy()
    radius = max(1, int(radius))
    axis = np.arange(-radius, radius + 1, dtype=np.float32)
    yy, xx = np.meshgrid(axis, axis, indexing="ij")
    return np.sqrt((xx * xx) + (yy * yy))


@lru_cache(maxsize=32)
def _core_kernel(core_radius_quarter_px: int):
    np = _need_numpy()
    core_radius = max(1.0, core_radius_quarter_px / 4.0)
    radius = int(math.ceil(core_radius + 1.0))
    distance = _kernel_grid(radius)
    alpha = 1.0 - _smoothstep(core_radius - 0.75, core_radius + 0.75, distance)
    return np.clip(alpha * 255.0, 0, 255).astype(np.uint16)


@lru_cache(maxsize=256)
def _glow_kernel(
    core_radius_quarter_px: int,
    glow_max_radius_quarter_px: int,
    opacity_u8: int,
    luma_bucket: int,
):
    np = _need_numpy()
    luma_bucket = max(0, min(63, int(luma_bucket)))
    if luma_bucket <= 0:
        return np.zeros((1, 1), dtype=np.uint16)

    core_radius = max(1.0, core_radius_quarter_px / 4.0)
    glow_max_radius = max(0.0, glow_max_radius_quarter_px / 4.0)
    glow_end = core_radius + (glow_max_radius * (luma_bucket / 63.0))
    radius = int(math.ceil(glow_end + 1.0))
    distance = _kernel_grid(radius)
    alpha = 1.0 - _smoothstep(core_radius, glow_end, distance)
    alpha *= max(0, min(255, opacity_u8)) / 255.0
    return np.clip(alpha * 255.0, 0, 255).astype(np.uint16)


def _blend_additive(image, center: tuple[int, int], color, alpha) -> None:
    np = _need_numpy()
    radius = alpha.shape[0] // 2
    center_x, center_y = center
    x0 = max(0, center_x - radius)
    x1 = min(image.shape[1], center_x + radius + 1)
    y0 = max(0, center_y - radius)
    y1 = min(image.shape[0], center_y + radius + 1)
    if x0 >= x1 or y0 >= y1:
        return

    ax0 = x0 - (center_x - radius)
    ay0 = y0 - (center_y - radius)
    alpha_view = alpha[ay0 : ay0 + (y1 - y0), ax0 : ax0 + (x1 - x0)]
    if int(alpha_view.max()) == 0:
        return

    region = image[y0:y1, x0:x1]
    tint = (
        alpha_view[..., None] * color.astype(np.uint16)[None, None, :] + 127
    ) // 255
    np.minimum(region + tint, 255, out=region)


def _blend_source_over(image, center: tuple[int, int], color, alpha) -> None:
    np = _need_numpy()
    radius = alpha.shape[0] // 2
    center_x, center_y = center
    x0 = max(0, center_x - radius)
    x1 = min(image.shape[1], center_x + radius + 1)
    y0 = max(0, center_y - radius)
    y1 = min(image.shape[0], center_y + radius + 1)
    if x0 >= x1 or y0 >= y1:
        return

    ax0 = x0 - (center_x - radius)
    ay0 = y0 - (center_y - radius)
    alpha_view = alpha[ay0 : ay0 + (y1 - y0), ax0 : ax0 + (x1 - x0)]
    if int(alpha_view.max()) == 0:
        return

    region = image[y0:y1, x0:x1]
    inverse = 255 - alpha_view
    tint = color.astype(np.uint16)[None, None, :]
    blended = (
        (region.astype("uint32") * inverse[..., None].astype("uint32")) +
        (tint.astype("uint32") * alpha_view[..., None].astype("uint32")) + 127
    ) // 255
    region[:] = blended.astype("uint16")


def _radial_geometry(trace: Trace) -> tuple[float, float]:
    geometry = trace.metadata.get("geometry")
    if not isinstance(geometry, dict):
        raise RuntimeError(
            "Radial view requires trace geometry metadata. Regenerate the trace."
        )

    inner_radius = geometry.get("inner_radius_mm")
    strip_length = geometry.get("radial_strip_length_mm")
    if not isinstance(inner_radius, (int, float)) or not isinstance(
        strip_length, (int, float)
    ):
        raise RuntimeError(
            "Radial view requires inner_radius_mm and radial_strip_length_mm "
            "geometry metadata. Regenerate the trace."
        )
    if inner_radius < 0 or strip_length <= 0:
        raise RuntimeError("Trace geometry metadata has invalid annulus dimensions")
    return float(inner_radius), float(strip_length)


def _radial_frame(
    source, led_size: int, spacing: float, glare: bool, geometry: tuple[float, float]
):
    np = _need_numpy()
    spokes, rings, _channels = source.shape
    inner_radius_mm, strip_length_mm = geometry
    led_size = max(2, int(led_size))
    spacing = max(1.05, float(spacing))

    layout = _radial_layout(
        spokes,
        rings,
        int(round(inner_radius_mm * 100.0)),
        int(round(strip_length_mm * 100.0)),
        led_size,
        int(round(spacing * 1000.0)),
    )
    image = np.zeros((layout.image_size, layout.image_size, 3), dtype=np.uint16)
    flat_source = source.reshape((-1, 3))
    active = np.nonzero(flat_source.max(axis=1))[0]

    core_radius_key = int(round(layout.core_radius * 4.0))
    core_alpha = _core_kernel(core_radius_key)

    if glare:
        glow_radius_key = int(round(layout.glow_max_radius * 4.0))
        glow_opacity = int(round(layout.glow_opacity * 255.0))
        luma_values = (
            (54 * flat_source[:, 0].astype(np.uint16)) +
            (183 * flat_source[:, 1].astype(np.uint16)) +
            (19 * flat_source[:, 2].astype(np.uint16)) + 128
        ) // 256
        for index in active:
            color = flat_source[index]
            luma_bucket = min(63, (int(luma_values[index]) * 63 + 127) // 255)
            if luma_bucket == 0:
                continue
            _blend_additive(
                image,
                layout.centers[index],
                color,
                _glow_kernel(
                    core_radius_key,
                    glow_radius_key,
                    glow_opacity,
                    luma_bucket,
                ),
            )

    for index in active:
        _blend_source_over(
            image,
            layout.centers[index],
            flat_source[index],
            core_alpha,
        )

    return image.astype(np.uint8)


def frame_image(
    trace: Trace,
    frame: int,
    plane: str = "rgb_final",
    scale: int = 12,
    glare: bool = False,
    layout: str = "heatmap",
    spacing: float = 1.35,
    show_frame_label: bool = True,
    brightness: int = 255,
):
    _need_numpy()
    source = trace.frame(frame, plane).copy()
    source = _apply_preview_brightness(source, brightness)
    scale = max(1, int(scale))
    if layout == "heatmap":
        image = _heatmap_frame(source, scale, glare)
    elif layout == "radial":
        image = _radial_frame(source, scale, spacing, glare, _radial_geometry(trace))
    else:
        raise ValueError(f"Unknown viewer layout {layout!r}")
    if show_frame_label:
        image = _add_frame_label(image, frame_label(trace, frame))
    return image


def save_ppm(path: str | pathlib.Path, image) -> None:
    path = pathlib.Path(path)
    height, width, channels = image.shape
    if channels != 3:
        raise ValueError("PPM export requires RGB image data")
    with path.open("wb") as out:
        out.write(f"P6\n{width} {height}\n255\n".encode("ascii"))
        out.write(image.tobytes())


def capture(
    trace: Trace,
    path: str | pathlib.Path,
    frame: int = 0,
    plane: str = "rgb_final",
    scale: int = 12,
    glare: bool = False,
    layout: str = "heatmap",
    spacing: float = 1.35,
    show_frame_label: bool = True,
    brightness: int = 255,
) -> None:
    path = pathlib.Path(path)
    image = frame_image(
        trace,
        frame,
        plane=plane,
        scale=scale,
        glare=glare,
        layout=layout,
        spacing=spacing,
        show_frame_label=show_frame_label,
        brightness=brightness,
    )
    if path.suffix.lower() == ".ppm":
        save_ppm(path, image)
        return

    os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")
    os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
    try:
        import pygame
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "PNG/JPEG capture requires pygame. Use a .ppm output path for the "
            "dependency-free still-frame exporter."
        ) from exc

    pygame.init()
    try:
        surface = pygame.surfarray.make_surface(image.swapaxes(0, 1))
        pygame.image.save(surface, str(path))
    finally:
        pygame.quit()


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
    content_size: tuple[int, int],
    desktop_size: tuple[int, int] | None,
) -> tuple[int, int]:
    content_width = max(1, content_size[0])
    content_height = max(1, content_size[1])
    if desktop_size is None:
        return content_width, content_height

    desktop_width, desktop_height = desktop_size
    max_width = max(320, int(desktop_width * 0.90))
    max_height = max(240, int(desktop_height * 0.86))
    scale = min(1.0, max_width / content_width, max_height / content_height)
    return max(1, int(content_width * scale)), max(1, int(content_height * scale))


def _aspect_fit_rect(
    pygame,
    content_size: tuple[int, int],
    window_size: tuple[int, int],
):
    content_width, content_height = content_size
    window_width, window_height = window_size
    if content_width <= 0 or content_height <= 0:
        return pygame.Rect(0, 0, max(1, window_width), max(1, window_height))

    scale = min(window_width / content_width, window_height / content_height)
    width = max(1, int(content_width * scale))
    height = max(1, int(content_height * scale))
    return pygame.Rect(
        (window_width - width) // 2,
        (window_height - height) // 2,
        width,
        height,
    )


def _blit_scaled_frame(pygame, screen, surface) -> None:
    screen.fill((0, 0, 0))
    rect = _aspect_fit_rect(pygame, surface.get_size(), screen.get_size())
    if rect.size == surface.get_size():
        screen.blit(surface, rect)
    else:
        screen.blit(pygame.transform.smoothscale(surface, rect.size), rect)


def play(
    trace: Trace,
    plane: str = "rgb_final",
    fps: float | None = None,
    max_fps: float = 60.0,
    scale: int = 12,
    glare: bool = False,
    layout: str = "heatmap",
    spacing: float = 1.35,
    show_frame_label: bool = True,
    brightness: int = 255,
) -> None:
    os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")
    os.environ.setdefault("SDL_VIDEO_MAC_FULLSCREEN_SPACES", "1")
    try:
        import pygame
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "Interactive playback requires pygame. The non-interactive "
            "'capture' command can still export .ppm still frames without it."
        ) from exc

    pygame.init()
    playback_fps = fps or trace.fps or 30.0
    frame_index = 0
    playing = True
    speed = 1.0
    frame_accumulator = 0.0
    clock = pygame.time.Clock()
    last_tick_s = pygame.time.get_ticks() / 1000.0

    image = frame_image(
        trace,
        frame_index,
        plane=plane,
        scale=scale,
        glare=glare,
        layout=layout,
        spacing=spacing,
        show_frame_label=show_frame_label,
        brightness=brightness,
    )
    height, width = image.shape[:2]
    window_flags = pygame.RESIZABLE
    screen = pygame.display.set_mode(
        _initial_window_size((width, height), _desktop_size(pygame)),
        window_flags,
    )
    pygame.display.set_caption(f"{trace.path.name} [{plane}]")

    try:
        running = True
        while running:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                elif event.type == pygame.KEYDOWN:
                    if event.key in (pygame.K_ESCAPE, pygame.K_q):
                        running = False
                    elif event.key == pygame.K_SPACE:
                        playing = not playing
                    elif event.key == pygame.K_RIGHT:
                        frame_index = min(frame_index + 1, trace.header.frame_count - 1)
                        frame_accumulator = 0.0
                        playing = False
                    elif event.key == pygame.K_LEFT:
                        frame_index = max(frame_index - 1, 0)
                        frame_accumulator = 0.0
                        playing = False
                    elif event.key == pygame.K_UP:
                        speed *= 1.25
                    elif event.key == pygame.K_DOWN:
                        speed = max(0.1, speed / 1.25)
                elif event.type == pygame.VIDEORESIZE:
                    screen = pygame.display.set_mode(
                        (event.w, event.h),
                        window_flags,
                    )

            now_s = pygame.time.get_ticks() / 1000.0
            elapsed_s = max(0.0, now_s - last_tick_s)
            last_tick_s = now_s
            if playing:
                frame_accumulator += elapsed_s * playback_fps * speed
                frame_steps = int(frame_accumulator)
                if frame_steps > 0:
                    frame_index = (
                        frame_index + frame_steps
                    ) % trace.header.frame_count
                    frame_accumulator -= frame_steps

            label = frame_label(trace, frame_index)
            image = frame_image(
                trace,
                frame_index,
                plane=plane,
                scale=scale,
                glare=glare,
                layout=layout,
                spacing=spacing,
                show_frame_label=show_frame_label,
                brightness=brightness,
            )
            surface = pygame.surfarray.make_surface(image.swapaxes(0, 1))
            _blit_scaled_frame(pygame, screen, surface)
            state = "play" if playing else "pause"
            pygame.display.set_caption(
                f"{trace.path.name} [{plane}] {layout} {label} {state} x{speed:.2f}"
            )
            pygame.display.flip()
            clock.tick(max(1.0, min(float(max_fps), playback_fps * speed)))
    finally:
        pygame.quit()
