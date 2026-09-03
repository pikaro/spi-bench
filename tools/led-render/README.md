# LED Offline Renderer

This tool renders production LED animations on the host without connecting to
ESP32 devices. It is intended for debugging animation behavior, layer blending,
topology mapping, and flicker/discontinuity issues before hardware validation.

## Commands

Build and render:

```sh
bin/led-render \
  --config tools/led-render/examples/center-wave-green.json \
  --output /tmp/center-wave.tled
```

The default host build profile is `legacy-full`. Select the production dense
surface or either owned half explicitly:

```sh
bin/led-render --profile dense-full --config tools/led-render/examples/center-wave-green.json --output /tmp/dense-full.tled
bin/led-render --profile dense-gpu0 --config tools/led-render/examples/center-wave-green.json --output /tmp/dense-gpu0.tled
bin/led-render --profile dense-gpu1 --config tools/led-render/examples/center-wave-green.json --output /tmp/dense-gpu1.tled
```

These profiles are host verification builds. They do not create alternate
PlatformIO environments or a reduced production geometry.

Run the production ownership/stitch regression across every checked-in
animation fixture:

```sh
bin/test-led-render-stitch
```

For each fixture this renders dense-full, dense-gpu0, and dense-gpu1 traces,
rejects writes outside either owned half, and proves that combining the two
halves exactly reconstructs every full trace plane.

Inspect:

```sh
bin/led-analyze summary /tmp/center-wave.tled --stats
bin/led-analyze flicker /tmp/center-wave.tled --hue-ping-pong
bin/led-analyze pixel /tmp/center-wave.tled --spoke 3 --radial 12
bin/led-analyze region /tmp/center-wave.tled --spokes 0:4 --radials 10:20
```

Export a still frame without `pygame`:

```sh
bin/led-analyze capture /tmp/center-wave.tled --frame 40 --output /tmp/frame.ppm --bloom
```

Interactive playback uses `pygame`:

```sh
bin/led-view /tmp/center-wave.tled --bloom
```

The default viewer layout is the analysis-oriented spoke/ring heatmap. Use the
radial layout for a more realistic umbrella preview with circular LEDs,
inter-LED spacing, and optional luminance-driven bloom:

```sh
bin/led-view /tmp/center-wave.tled --layout radial --scale 10 --spacing 1.35 --bloom
bin/led-view /tmp/center-wave.tled --capture /tmp/frame.png --frame 80 --layout radial --bloom
```

Preview brightness defaults to `255`, matching the trace's unscaled RGB data.
Use `--brightness 96` to preview a lower logical display brightness.
This viewer scaling does not emulate ordinary SK9822's 5-bit hardware
quantization. `--glare` remains accepted as an alias for `--bloom`.
Interactive playback caps displayed frames with `--max-fps` and skips trace
frames as needed to preserve playback timing. `bin/led-view-pretty` uses the
radial bloom view at `--scale 10 --max-fps 30`.

Frame labels are enabled by default in both playback and capture. Disable them
with `--no-frame-label`.

Playback controls:

- Space: play/pause
- Left/right: step frames while paused
- Up/down: playback speed
- `q` or Escape: quit

The same pieces are available from Python:

```python
from led_render import Trace, run_render
from led_render.analysis import detect_flicker

run_render("tools/led-render/examples/center-wave-green.json", "/tmp/center.tled")
with Trace("/tmp/center.tled") as trace:
    print(detect_flicker(trace))
```

Python dependencies:

- Required for analysis and frame access: `numpy`
- Required for interactive playback and PNG/JPEG capture: `pygame`

The wrapper scripts use `python3` from the shell path. They do not activate the
project venv, because the viewer dependency may be supplied by the system/Nix
Python environment.

## Configuration

The C++ renderer accepts JSON. A single animation can be rendered from the root
object:

```json
{
  "animation": "CenterWave",
  "duration_ms": 1200,
  "layer": "TransientEffect",
  "frames": "0:180",
  "mode": "pipeline",
  "config": {
    "hue": 96,
    "saturation": 255,
    "value": 180,
    "rise": 2,
    "peak": 2,
    "wake": 5,
    "peakDelta": 1,
    "speedDelta": 1,
    "spokeModulo": 4
  }
}
```

Multiple animation requests can be sequenced with `animations`. Each entry has
the same `animation`, `config`, `start_ms`, `duration_ms`, and `layer` fields.
This is enough to reproduce adjacent-wave cases without building a full PubSub
script runner.

Input snapshots are supplied under `inputs`:

```json
{
  "inputs": {
    "wheel": {"position": 8192, "delta": 0},
    "fft": {"subBass": 64, "bass": 180, "lowMid": 220, "mid": 160},
    "peak": {"group": "Bass", "energy": 220, "frameSequence": 7}
  }
}
```

Missing FFT bands default to zero. Peak groups may be `Bass`, `Mid`, or
`High`, or the raw enum integer.

Synthetic audio timelines can be supplied at the root. They generate repeatable
FFT input while rendering and are useful for audio-reactive fixtures:

```json
{
  "audio": {
    "bass": {"base": 32, "amp": 180, "period_ms": 900},
    "mid": {"base": 28, "amp": 120, "period_ms": 1400, "phase": 48},
    "high": {"base": 16, "amp": 110, "period_ms": 420, "phase": 96}
  }
}
```

## Registry Generation

`bin/led-render-build` runs `tools/led-render/generate_registry.py` before
compiling the host renderer. The generator scans
`include/LedDisplay/Animations/*/Animation.hpp` plus each sibling `Config.hpp`
and discovers animation classes by the current production convention:

- one `struct WIRE_MSG NameConfig`
- one `struct Name` with `NameConfig config{}`
- static `defaultLayer`, `defaultLifetimeMs`, `defaultStyle`, and
  `requiresFullFrame`
- a `render(AnimationRenderContext &ctx)` method

The generated registry lives under `tools/led-render/generated/` and is not the
source of truth. Adding an animation should require adding the animation
directory headers. `Command.hpp` and `CommandDesc.hpp` are intentionally outside
the host-render discovery path.

## Trace Format

Traces use the `.tled` binary format:

```text
TraceHeader
metadata JSON
frame 0 planes
frame 1 planes
...
```

Pixels are stored in logical `(spoke, radial)` order. Each plane stores one
three-byte pixel after another. Initial planes are:

- `hsv_final`: composed firmware-style HSV frame
- `rgb_final`: deterministic host RGB conversion through `GenericRenderer`
- `hsv_scratch`: optional last animation scratch frame, enabled with
  `--include-scratch`

The metadata JSON records the source config, render mode, animation names,
frame range, topology, annular geometry, color backend, and each animation's
`requires_full_frame` trait. Radial viewer output uses this geometry metadata;
regenerate traces after geometry changes.

## Fidelity Notes

The renderer uses production animation, primitive, layer stack, compositor,
topology, and `GenericRenderer` code. It does not yet emulate FastLED temporal
dithering. A flicker visible in `hsv_final` is likely in animation/composition
logic. A flicker only visible on hardware still needs FastLED or electrical
validation. The firmware SK9822 byte encoder has a separate exhaustive C++ host
test; `.tled` traces remain logical animation/color traces, not captured wire
frames.
