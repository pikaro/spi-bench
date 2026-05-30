from __future__ import annotations

import math
import os
import pathlib

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


def _heatmap_frame(source, scale: int, glare: bool):
    np = _need_numpy()
    if glare:
        source = _apply_heatmap_glare(source)
    return np.repeat(np.repeat(source, scale, axis=0), scale, axis=1)


def _add_disc(image, center_x, center_y, radius, color, alpha=1.0):
    np = _need_numpy()
    radius = max(1.0, float(radius))
    x0 = max(0, int(math.floor(center_x - radius)))
    x1 = min(image.shape[1], int(math.ceil(center_x + radius + 1)))
    y0 = max(0, int(math.floor(center_y - radius)))
    y1 = min(image.shape[0], int(math.ceil(center_y + radius + 1)))
    if x0 >= x1 or y0 >= y1:
        return

    yy, xx = np.ogrid[y0:y1, x0:x1]
    mask = ((xx - center_x) ** 2 + (yy - center_y) ** 2) <= (radius * radius)
    if not mask.any():
        return

    color_value = color.astype(np.uint16)
    if alpha < 1.0:
        color_value = (color_value * alpha).astype(np.uint16)
    region = image[y0:y1, x0:x1].astype(np.uint16, copy=False)
    region[mask] = np.maximum(region[mask], color_value)
    image[y0:y1, x0:x1] = np.clip(region, 0, 255).astype(np.uint8)


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
    led_radius = led_size / 2.0
    pitch = led_size * spacing
    strip_length = rings * pitch
    inner_radius = (inner_radius_mm * strip_length) / strip_length_mm
    outer_radius = inner_radius + strip_length
    margin = max(led_size * 4, int(led_radius * (6 if glare else 3)))
    image_size = int(math.ceil((outer_radius + led_radius + margin) * 2.0))
    center = image_size / 2.0
    image = np.zeros((image_size, image_size, 3), dtype=np.uint8)

    centers = []
    for spoke in range(spokes):
        angle = (-math.pi / 2.0) + ((2.0 * math.pi * spoke) / spokes)
        angle_cos = math.cos(angle)
        angle_sin = math.sin(angle)
        for radial in range(rings):
            color = source[spoke, radial]
            if int(color.max()) == 0:
                continue
            radius = inner_radius + ((radial + 0.5) * pitch)
            x = center + (angle_cos * radius)
            y = center + (angle_sin * radius)
            centers.append((x, y, color))

    if glare:
        for x, y, color in centers:
            _add_disc(image, x, y, led_radius * 2.8, color, alpha=0.20)
            _add_disc(image, x, y, led_radius * 1.8, color, alpha=0.35)

    for x, y, color in centers:
        _add_disc(image, x, y, led_radius, color, alpha=1.0)

    return image


def frame_image(
    trace: Trace,
    frame: int,
    plane: str = "rgb_final",
    scale: int = 12,
    glare: bool = False,
    layout: str = "heatmap",
    spacing: float = 1.35,
    show_frame_label: bool = True,
):
    _need_numpy()
    source = trace.frame(frame, plane).copy()
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


def play(
    trace: Trace,
    plane: str = "rgb_final",
    fps: float | None = None,
    scale: int = 12,
    glare: bool = False,
    layout: str = "heatmap",
    spacing: float = 1.35,
    show_frame_label: bool = True,
) -> None:
    os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")
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
    clock = pygame.time.Clock()

    image = frame_image(
        trace,
        frame_index,
        plane=plane,
        scale=scale,
        glare=glare,
        layout=layout,
        spacing=spacing,
        show_frame_label=show_frame_label,
    )
    height, width = image.shape[:2]
    screen = pygame.display.set_mode((width, height))
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
                        playing = False
                    elif event.key == pygame.K_LEFT:
                        frame_index = max(frame_index - 1, 0)
                        playing = False
                    elif event.key == pygame.K_UP:
                        speed *= 1.25
                    elif event.key == pygame.K_DOWN:
                        speed = max(0.1, speed / 1.25)

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
            )
            surface = pygame.surfarray.make_surface(image.swapaxes(0, 1))
            screen.blit(surface, (0, 0))
            state = "play" if playing else "pause"
            pygame.display.set_caption(
                f"{trace.path.name} [{plane}] {layout} {label} {state} x{speed:.2f}"
            )
            pygame.display.flip()

            if playing:
                frame_index = (frame_index + 1) % trace.header.frame_count

            clock.tick(max(1.0, playback_fps * speed))
    finally:
        pygame.quit()
