# Animation Pipeline Offline Renderer Plan

This is an implementation plan for local host-side animation rendering,
analysis, and playback tooling. It is intentionally independent of ESP32 device
connectivity.

The goal is not a decorative preview. The goal is a deterministic renderer that
shares the real animation, primitive, topology, compositor, and color code where
reasonable, emits inspectable binary traces, and gives us statistical tools for
finding flicker, discontinuities, mapping errors, and blend artifacts.

## Objectives

- Render frames `N..M` for one animation with parameters supplied externally.
- Store the result in a simple binary trace that can be streamed and analyzed.
- Keep animations and primitives host-compilable with minimal dependencies.
- Provide analysis tools for spatial regions, per-pixel time series, and
  flicker/discontinuity detection.
- Provide a Python interface that can configure animations, invoke the renderer,
  load traces, analyze them, and display them in a small video player.
- Reflect firmware behavior closely enough that findings are meaningful before
  hardware validation.

## Non-Goals

- Do not require ESP32 hardware, serial devices, PlatformIO upload, or monitor
  access.
- Do not build a second animation implementation in Python.
- Do not let host-only convenience APIs leak back into animation classes.
- Do not claim exact FastLED temporal-dither fidelity until that path is proven
  against either FastLED host code or hardware captures.
- Do not replace eventual hardware validation. The host renderer reduces the
  search space; it does not prove LED electrical behavior.

## Architecture

The host renderer is a small C++ executable plus Python wrappers:

- `tools/led-render/host_render.cpp`: command-line renderer.
- `tools/led-render/TraceFormat.hpp`: binary format constants and structs.
- `tools/led-render/HostRuntime.hpp`: minimal host render runtime.
- `tools/led-render/Json.hpp`: minimal JSON parser and type readers.
- `tools/led-render/generate_registry.py`: source scanner for the generated
  host animation registry.
- `tools/led-render/generated/AnimationRegistry.hpp`: generated dispatch and
  config parser output.
- `tools/led-render/README.md`: tool-specific usage.
- `tools/led_render_py/`: Python package for invocation, trace loading,
  analysis, and playback.
- `bin/led-render`: repo wrapper for the C++ renderer.
- `bin/led-view`: repo wrapper for the Python viewer.
- `bin/led-analyze`: repo wrapper for non-interactive analysis.

The renderer must not hardcode the currently existing animation set. Adding a
new animation should require adding the animation header/config itself, not
editing host-renderer switches in several places. The implementation should use
one of these discovery paths:

1. Preferred: generate a host animation manifest from source annotations.
   This can reuse the same broad shape as `WIRE_MSG`: add an annotation such as
   `LED_ANIMATION` to each animation class or config, then add a Makefile
   target that emits a generated host registry.

2. Current implementation: derive the manifest from `include/LedDisplay/Animations/`
   and lightweight naming conventions. This is viable because animations live
   in one directory and each animation owns its config, command declaration, and
   render/update behavior.

3. Avoid: a manually maintained host-only list of animation names, config
   parsers, and dispatch functions.

The C++ renderer should include the production animation headers directly:

- `include/LedDisplay/Animations/*.hpp`
- `include/LedDisplay/Primitives/Canvas.hpp`
- `include/LedDisplay/detail/Compositor.hpp`
- `include/LedDisplay/detail/LayerStack.hpp` where whole-pipeline rendering is
  needed
- `include/LedDisplay/Renderers/GenericRenderer.hpp`
- `include/LedTopology/Facade.hpp`

The host runtime supplies only what is genuinely external to an isolated
animation:

- frame clock
- hue/rotation offsets
- logical-to-local map
- scratch/output buffers
- optional FFT and wheel input snapshots
- config payload loaded from JSON

Animations should not depend on services, tasks, PubSub, queues, logging, or
device output. If a production animation cannot compile in this host runtime,
that is a design smell and should be fixed in the animation boundary rather
than emulated broadly.

## Rendering Modes

There are two modes:

1. `animation`
   Render one animation directly into a scratch-sized frame through
   `AnimationRenderContext`. This mode tests isolated animation behavior and
   primitive usage.

2. `pipeline`
   Render the animation through the same scratch -> layer -> composed-frame path
   used by firmware. This mode tests layer policy, blend behavior, decay, and
   final HSV composition.

The current implementation also supports a small generic sequence format:
`animations: [...]` entries with `start_ms`, `duration_ms`, `layer`, and
`config`. This is intentionally much smaller than a PubSub script runner, but
is enough for adjacent animation regression fixtures.

## Animation Selection And Parameters

The CLI should accept JSON configuration instead of positional flags for
animation-specific fields:

```sh
bin/led-render \
  --animation CenterWave \
  --config examples/center-wave-green.json \
  --frames 0:180 \
  --fps 125 \
  --mode pipeline \
  --output /tmp/center-wave.totemled
```

Each config file should contain:

```json
{
  "animation": "CenterWave",
  "duration_ms": 1200,
  "layer": "Effect",
  "hue_offset": 0,
  "rotation_offset": 0,
  "config": {
    "hue": 96,
    "saturation": 255,
    "value": 180,
    "rise": 2,
    "peak": 1,
    "wake": 5
  }
}
```

The parser should map animation names using `magic_enum`. It should reject
unknown fields and type mismatches so traces are reproducible.

The JSON parser should be generated or table-driven from the same manifest used
for dispatch. It must not have one bespoke parser branch per currently existing
animation unless that branch is emitted by a generator. Generated parsing can
start modestly: map JSON fields onto `WIRE_MSG` config fields and use explicit
per-field conversion for integers, booleans, enum names, and nested value types
as they become necessary.

## Binary Trace Format

Use a compact little-endian binary format with a fixed header and contiguous
frame payloads. Store pixels temporally first because it is stream-friendly:

```text
Header
Frame 0 plane(s)
Frame 1 plane(s)
...
Frame K plane(s)
```

Initial header fields:

- magic: `TLED`
- version: `1`
- header size
- flags
- frame count
- first frame index
- fps numerator and denominator
- timestamp step in microseconds
- topology: strip count, segments per strip, spokes, rings
- pixel counts: logical total, local owned count
- plane mask
- per-frame record size
- metadata JSON byte length

Initial planes:

- `hsv_final`: final composed HSV frame, 3 bytes per pixel
- `rgb_final`: final RGB frame, 3 bytes per pixel
- optional `hsv_scratch`: raw animation scratch frame
- optional `hsv_layer_<name>`: selected layer frames

Pixel order should be logical `(spoke, radial)` for full-topology traces unless
the trace explicitly says it is local owned order. Logical order is better for
analysis and viewing because it is stable across GPU ownership. The renderer can
also emit local-owned traces later for firmware parity checks.

The metadata JSON should include:

- renderer command line
- git commit if available
- animation name
- animation config JSON
- render mode
- topology and ownership mode
- color backend
- FastLED fidelity mode
- build timestamp

## Color Backends

Start with `GenericRenderer` HSV->RGB conversion. It is deterministic and
already host-friendly.

Add a `--color-backend fastled` option only after proving FastLED host
compilation is practical. The target is to reuse FastLED's conversion and,
where possible, temporal dithering behavior. If FastLED host support turns out
to pull in too much platform state, keep it behind an optional build target and
document the gap.

Do not tune firmware behavior to match the host renderer. The renderer follows
firmware; it is not a second source of truth.

## Temporal Dithering Plan

Temporal dithering is the highest-fidelity risk.

Implementation sequence:

1. Emit raw final HSV frames.
2. Emit deterministic RGB frames from `GenericRenderer`.
3. Add optional FastLED HSV->RGB conversion if host compilation is clean.
4. Investigate FastLED temporal dithering separately with a focused fixture.
5. Compare host output against captured hardware or firmware-emitted binary
   frames before declaring dither fidelity.

Until step 5, flicker analysis should distinguish:

- HSV/layer flicker: likely animation/composition bug
- deterministic RGB flicker: likely color conversion or value discontinuity
- hardware-only flicker: likely FastLED temporal dithering, timing, power, or
  electrical behavior

## Python Package

The Python side should be a thin control and inspection layer:

- `led_render.run_render(...)`: invoke `bin/led-render`
- `led_render.Trace`: memory-map or load binary traces
- `Trace.frames(plane="hsv_final")`
- `Trace.pixel_series(spoke, radial, plane="hsv_final")`
- `Trace.region(spokes=..., radials=..., plane=...)`
- `led_render.analysis.detect_flicker(trace, ...)`
- `led_render.analysis.detect_hue_ping_pong(trace, ...)`
- `led_render.analysis.frame_deltas(trace, ...)`
- `led_render.viewer.play(trace, ...)`

Use NumPy for trace loading and analysis. Use `pygame` for interactive
playback and PNG/JPEG capture because it gives direct access to the rendered
pixel surface and simple real-time controls. The tooling also includes a
dependency-light `.ppm` still-frame exporter so capture is not blocked when
`pygame` is unavailable.

Viewer controls:

- play/pause
- frame step forward/backward
- speed control
- frame slider
- plane selector
- color mode selector
- pixel hover/readout if the backend supports it
- optional spoke/ring grid overlay

The viewer should display the umbrella topology rather than a flat strip where
possible. A flat `(spoke x radial)` heatmap is acceptable as the first version
because it is analysis-friendly.

## Analysis Tools

First analysis commands:

```sh
bin/led-analyze summary /tmp/trace.totemled
bin/led-analyze flicker /tmp/trace.totemled --plane hsv_final
bin/led-analyze pixel /tmp/trace.totemled --spoke 3 --radial 12
bin/led-analyze region /tmp/trace.totemled --spokes 0:4 --radials 10:20
```

Initial metrics:

- per-frame max delta in hue, saturation, value, RGB channels
- per-pixel max delta over time
- isolated spike detection where one pixel changes sharply but neighbors do not
- hue ping-pong detection between two hue clusters
- value spike detection, especially flashes to high value/low saturation
- temporal variance heatmap
- frame-to-frame discontinuity report with top offending pixels

Flicker detection should be configurable. A useful starting heuristic:

- compute per-pixel frame-to-frame RGB and HSV deltas
- flag pixels whose delta exceeds a percentile and whose neighboring pixels do
  not show the same coherent motion
- flag rapid A/B/A hue alternation over three or more frames
- flag sudden saturation collapse plus value increase, which catches white or
  pink flashes from blended hues

## Fixtures

Create checked-in sample configs under `tools/led-render/examples/`:

- `center-wave-green.json`
- `center-wave-purple.json`
- `center-wave-green-purple-adjacent.json`
- `wheel-indicator-static.json`
- `spoke-sweep.json`
- `fft-reactive-static.json`

The adjacent-wave case should become the first regression fixture for the
reported pink/white flicker issue. It should render two waves that follow each
other immediately with no intentional gap.

## Build Integration

Prefer a simple host build command before adding complex build-system support:

```sh
bin/led-render-build
```

The wrapper can call `c++` directly with:

- C++23
- `-Iinclude`
- host-only defines for LED ownership/topology defaults
- no PlatformIO or ESP-IDF include paths

Add a generator target if the manifest approach is used:

```sh
make led-render-registry
```

That target should write generated files under `include/Generated/LedRender/`
or `tools/led-render/generated/`. Generated files should include animation
headers, construct configs from JSON, expose default metadata, and dispatch
render/update through the production animation types.

If direct compilation exposes too many embedded dependencies, fix the offending
animation/primitives boundaries first. Add a CMake target only if the direct
wrapper becomes hard to maintain.

## Implementation Sequence

1. Done: use directory scanning for the first generated animation manifest.
2. Done: make the host renderer consume that registry instead of hardcoding
   animation classes.
3. Done: add trace format header and host runtime helpers.
4. Done: every discovered animation compiles in a host-only translation unit.
5. Done: add the C++ renderer CLI with generated/table-driven JSON config
   parsing.
6. Done: emit `hsv_final` traces in logical topology order.
7. Done: add `rgb_final` with `GenericRenderer`.
8. Done: add Python trace loader and summary analysis.
9. Done: add pixel/region/time-series extraction commands.
10. Done: add flicker and hue ping-pong analysis.
11. Done: add `pygame` playback plus dependency-light still-frame export.
12. Done: add examples and document expected commands.
13. Pending: investigate optional FastLED host backend.
14. Done: add adjacent-wave regression fixture and baseline analysis command.

## Acceptance Criteria

- `bin/led-render` can render a named animation over a frame range from JSON.
- The output trace can be loaded without copying the whole file for normal
  inspection.
- The trace records enough metadata to reproduce the render command.
- Python can extract one pixel's time series and a rectangular spoke/ring
  region.
- Python can produce a flicker/discontinuity report from a trace.
- Python can play the trace with pause, step, and speed controls.
- Host rendering uses production animation and primitive code, not duplicated
  Python animation logic.
- Adding a new animation does not require editing host renderer dispatch code;
  the animation is discovered through the manifest/scanning mechanism.
- The documented adjacent-wave fixture can be rendered and analyzed locally.

## Open Questions

- Whether the first trace should store all layers by default or only final
  frames plus optional selected layers. The conservative start is final frames
  only, with opt-in layer dumps.
- Whether host traces should default to full logical topology or per-GPU owned
  topology. The conservative start is full logical topology.
- Whether FastLED host compilation is worth the dependency cost. Start without
  it, then evaluate with a focused spike.
- Whether the viewer should first use a spoke/ring heatmap or a geometric
  umbrella layout. Start with a heatmap, then add geometry if needed.
- Whether animation discovery should be source-annotation based from the start
  or initially derived from the single animation directory. Source annotations
  are more robust; directory scanning is faster to bootstrap.
