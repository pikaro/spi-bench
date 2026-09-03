# Animation Pipeline

This is the current design reference for GPU-side LED animation rendering.
It consolidates the current embedded pipeline documentation and the legacy
offline-render tooling plan into one active reference. Advanced animation plans
and known visual issues are intentionally out of scope for this document.

The pipeline is intentionally not backwards compatible with the first bring-up
implementation or with the old `../led` codebase. That code was used only as an
architectural reference. The objective here is the clean final shape for the
current firmware.

Planned procedural sprite work for star, heart, and smiley animations is tracked
separately in [sprite-animation-plan.md](sprite-animation-plan.md).

## Current Status

The embedded pipeline is feature-complete for current animation development:

- animation commands are split into playback, update, and specific control
  messages instead of sharing one catch-all command shape
- `Display` owns lifecycle, strobe accounting, present buffering, and output
  presentation only
- `AnimationEngine` owns command draining, animation slots, input snapshots,
  layer rendering, and concrete animation dispatch
- concrete animations live under `include/LedDisplay/Animations/<Name>/` with
  separate config, command, command-description, and render headers
- reusable drawing access lives under `include/LedDisplay/Primitives/`
- rendering uses scratch -> layer -> composed frame
- the topmost `UI` layer retains its last frame and decays it over roughly two
  seconds, so stopping a UI animation produces the shared fade-out behavior
- present buffering supports `None`, `Double`, `Triple`, and `Quadruple`;
  the current default is `Triple`
- production GPU builds select the 32×60 `DenseUmbrella` topology and own one
  contiguous 16-spoke/960-pixel half each
- `Display` uses a compile-time-selected output backend: legacy WS2812B remains
  FastLED-controlled, while v2 SK9822 output uses a pure encoder and ESP-IDF
  SPI3 transport with 5-bit hardware brightness
- FFT, peak, and wheel data are latest-value input streams for animations, not
  display-level rendering special cases
- the engine derives a small per-frame audio-control snapshot from FFT bands
  and peak events so animations can use smoothed bass/mid/high/energy plus
  short attack pulses without owning PubSub state
- the persistent wheel indicator starts through the same generic command path
  as all other animations

Remaining pipeline work is physical validation, performance acceptance, and
later visual tuning, not another structural replacement:

- keep using the host-side render tooling under `tools/led-render/` for local
  animation inspection and regression traces
- optionally extend the host renderer with stricter FastLED color/dither parity
  if a future legacy-output investigation needs it
- validate SK9822 waveform, RGB order, low-light brightness, startup behavior,
  and sustained 100 fps timing on the production-code 2×120 physical bench
- tune layer decay, opacity, and blend choices with hardware and offline traces
- revisit parallel per-layer rendering only if production dense metrics prove
  the sequential renderer is the bottleneck

## Responsibilities

`Display` is the hardware presentation owner. It initializes the output backend,
subscribes to animation play, update, and specific control PubSub topics,
delegates input subscriptions to the engine, handles the present-strobe ISR,
records present timing, and calls
`SelectedOutput::show()` with complete frames. The selected backend owns
protocol conversion, brightness policy, framing, and transport. `Display` must
remain unaware of concrete animations such as waves, FFT visuals, or the wheel
indicator.

`AnimationEngine` is the animation backend. It validates and queues commands,
drains them at frame boundaries, owns active slots, captures latest FFT, peak,
and wheel inputs, updates the audio-control snapshot, renders each active
animation into scratch, blends scratch into the target layer, composes layers
into the output frame, and expires timed animations.

`Animations/<Name>/Config.hpp` owns the queue-copyable config payload and shared
animation metadata such as kind, default layer, and default lifetime. It must not
include command-codec, PubSub, or renderer dependencies.

`Animations/<Name>/Animation.hpp` owns render behavior for that animation and
depends only on the config plus rendering interfaces. `Command.hpp` owns command
construction and payload encoding. `CommandDesc.hpp` owns the console handler and
subcommand descriptor. `CoreCommands.hpp` only includes per-animation
`CommandDesc.hpp` files and combines their descriptors into the `/anim`
subcommand array. The registry is boring glue: it maps `AnimationKind` to
payload construction, style lookup, update dispatch, and render dispatch.

`Primitives/*` are reusable drawing kernels and canvas accessors. A primitive
renders into a supplied canvas/scratch buffer and does not own lifetime, command
payloads, PubSub topics, layers, or animation slots. Full-scene behavior belongs
in animations, not primitives.

`LayerStack` owns the fixed layer buffers plus one shared scratch buffer.
Animations render sequentially, so one scratch buffer is enough: clear scratch,
render one animation, blend scratch into that animation's layer, then reuse
scratch for the next animation.

`Compositor` owns blend semantics. Current blend operations are `Replace`,
`MaxValue`, `AddValue`, and `Alpha`.

`PresentBuffers` owns complete-frame handoff. It keeps presentation from using a
partially rendered frame and records repeated presents when no newer frame is
ready.

`src/master/orchestration.hpp` maps system events into generic LED requests. It
does not own animation policy. For example, master can publish wheel indicator
updates, but the wheel indicator animation decides whether and how those
updates affect pixels. The main IO dial's normalized value is forwarded as the
GPU global-brightness command, while its exact 0..31 position starts the
short-lived brightness indicator on the UI layer. Master also starts the
managed FFT visual sequence after startup, periodically refreshes the active
persistent request, and crossfades between `Fft` and `FftAlt` layers using
stable request IDs. A stop-all command or a stop for either managed FFT request
suppresses further FFT visual sequencing until the master restarts, so manual
diagnostics can actually silence the field. Master uses peak events for
lightweight orchestration:
ordinary peak events are not center-wave requests, but the first peak after a
two-second quiet window publishes one short center wave as a primitive drop
marker.

`src/master/led_bringup.hpp` owns the master-triggered LED startup sequence.
Each GPU initializes its local SK9822 output and periodically publishes an
output-ready event. Master waits for readiness from both GPU0 and GPU1, then
publishes the shared output-enable command followed by the spoke-sweep boot
animation. Automatic runtime orchestration remains held until that animation
finishes. The sweep runs against an untransformed logical-to-physical map;
otherwise normal effects such as wheel rotation, beat waves, or brightness
modulation can obscure topology diagnostics. Manual `/anim` commands remain
direct diagnostics.

The selected production `DenseUmbrella` topology has 32 spokes and 60 radial
LEDs per spoke. Physical order is monotonic by spoke and serpentine radially:

```text
physical = spoke * 60 + (odd(spoke) ? 59 - radial : radial)
```

GPU0 owns spokes 0..15 (physical 0..959) and GPU1 owns spokes 16..31
(physical 960..1919). Each maps its owned interval to local indices 0..959.
The legacy four-spoke-quadrant topology remains available through compile-time
selection for regression and old hardware; animations only see the selected
logical topology.

The physical LED surface is an annulus, not a disk. The strips do not meet at a
center point. The production SK9822 geometry has an approximately 60 mm inner
radius (120 mm center-gap diameter). Sixty LEDs at 144 LEDs/m occupy roughly
417 mm radially and run almost the full umbrella radius. Logical radial index
`0` is therefore the inner visible ring, not the geometric center. Geometry
helpers and radial viewers model the topology's own inner radius plus strip
length instead of assuming normalized radius starts at zero. The legacy
WS2812B topology retains its separate approximate 300 mm gap-diameter and
300 mm strip-length geometry.

## Frame Flow

Each GPU frame follows this path:

1. The present strobe wakes the display task.
2. Buffered modes select and present the newest complete frame through the
   compile-time output backend.
3. For SK9822, the backend converts HSV to RGB, applies optional output floors,
   writes one quantized 5-bit hardware level into every pixel header, and sends
   the complete 3,968-byte owned-half frame through SPI3.
4. The display asks the engine to render the next complete frame.
5. The engine drains queued commands.
6. The layer stack clears or decays layer buffers according to layer policy.
7. The engine snapshots latest FFT, peak, and wheel input state and updates
   audio controls.
8. Each active animation renders into shared scratch.
9. Scratch is blended into the animation's target layer using the animation's
   style.
10. Layers are composed in fixed order into the present buffer render target.
11. Timed animations expire after composition.

Command and PubSub paths use the same engine:

- playback requests publish `AnimationPlayCommand` on `Topic::AnimationPlay`;
  they carry animation kind, request ID, layer, lifetime, and the concrete
  animation payload
- update requests publish `AnimationUpdateCommand` on `Topic::AnimationUpdate`;
  they carry animation kind, request ID, and the concrete animation payload, but
  no layer or lifetime
- control requests publish their own payload structs on their own topics, such
  as `AnimationStopCommand` on `Topic::AnimationStop`,
  `AnimationSetHueOffsetCommand` on `Topic::AnimationSetHueOffset`, and
  `AnimationFadeLayerSwapCommand` on `Topic::AnimationFadeLayerSwap`
- GPUs subscribe to the animation command topics and enqueue the typed command
- `Play` creates a slot, `Update` mutates matching active slots, and `Stop`
  disables matching slots

Updates are matched by animation kind and optional request ID. Request ID `0`
means update every active animation of that kind. Misses are counted in
`ledDisp.updMiss`.

`Play` with a nonzero request ID replaces any currently active slot with that
same request ID. This makes repeated command delivery idempotent for persistent
animations such as `WheelIndicator`. `Play` with request ID `0` allocates a
fresh nonzero request ID.

## Render Traits

Each animation exposes `static constexpr bool requiresFullFrame`. Normal
animations leave this `false` and render only the pixels owned by the current
GPU, using the existing logical-to-local map. Animations that inherently need a
complete logical frame can set it to `true`; the engine then renders into a
full logical scratch frame and projects the owned pixels back into the local
layer scratch before blending.

The full-frame path exists for future effects such as feedback warping where
previous or neighboring pixels across GPU boundaries are intrinsic to the
effect. It should remain opt-in because it doubles render-side pixel work on
the current two-GPU layout. The current FFT visuals, wheel indicator, waves,
and diagnostics do not require full-frame rendering.

## Layers

Layers are fixed and composed in enum order:

- `Background`
- `Fft`
- `FftAlt`
- `Effect`
- `TransientEffect`
- `Wheel`
- `Debug`
- `UI`

Current default policies:

- `Background`: cleared each frame, `MaxValue`, full opacity
- `Fft`: persistent decay, `MaxValue`, full opacity
- `FftAlt`: persistent decay, `MaxValue`, full opacity, disabled by default
- `Effect`: persistent decay, `MaxValue`, full opacity
- `TransientEffect`: cleared each frame, `MaxValue`, full opacity
- `Wheel`: cleared each frame, `Alpha`, partial opacity
- `Debug`: persistent decay, `AddValue`, full opacity
- `UI`: persistent one-value-per-frame decay, `Replace`, full opacity

These defaults are tuning values. The ownership boundary is the important part:
backgrounds, FFT, one-shot effects, wheel overlays, diagnostics, and UI never
fight inside a single shared frame buffer. `UI` is last in the fixed composition
order, so its nonzero pixels always replace every lower layer. At the 100 Hz
frame rate, its decay from 255 to zero takes about 2.55 seconds. A master-owned
UI flow can therefore stop its animation and rely on one consistent GPU-local
fade instead of publishing a timed opacity sequence. Starting a new UI
animation clears retained output from the previous UI state before all active
UI animations render, preventing a shorter replacement (such as a decreased
brightness fill) from leaving the old geometry behind during the fade.

Layer active state and opacity are runtime controls. `/layer active <Layer>
<on|off>` toggles a layer; disabling clears that layer once and then skips its
per-frame clear/decay and compose work. `/layer opacity <Layer> <0-255>` sets
the layer's compose opacity. The master publishes per-layer startup active
commands from `src/master/orchestration.hpp`; the current startup policy keeps
`Background`, `FftAlt`, and `Wheel` disabled until a show or manual command
enables them.

`/layer swap <Layer> <Layer> <durationMs>` starts a GPU-local opacity fade
between two layers. It requires one of the two layers to be at opacity `255`
and enabled, and the other to be at opacity `0`; the GPU infers the fade
direction from those endpoint opacities. Both layers are enabled during the
fade. When the duration completes, the faded-out layer is set to opacity `0`,
disabled, cleared by the disable path, and any animations still running on that
layer are stopped.

## Current Animations

`CenterWave` is a one-shot effect on the `TransientEffect` layer. Its config
has separate rise, peak, and wake ring counts. It also supports per-spoke width
and speed modulation through a triangular modulo profile. In this animation,
`peak` is the bright wave width; `peakDelta` and `speedDelta` default to `0`,
so plain waves are unmodulated. Passing `peakDelta = 1`, `speedDelta = 1`, and
`spokeModulo = 4` makes spokes `0..4` resolve to offsets `0, 0.5, 1, 0.5, 0`,
so with `peak = 2` the peak width becomes `2, 2.5, 3, 2.5, 2` across that
cycle while `rise` remains unchanged. The layer clears every frame, so the
visible wave width follows the animation profile instead of inheriting the
persistent `Effect` layer's decay wake.

`Starburst` is a one-shot pointed burst on the `TransientEffect` layer. It
reuses the center-wave rise/peak/wake profile but derives a triangular angular
point scale from `points`, `twist`, and the logical spoke/ring position. High
point-scale spokes get extra peak width and extra travel distance through
`pointGain`, while low point-scale spokes are faded out before drawing. The
burst tips therefore lead, broaden, and stay brighter than the gaps.
`points = 4` is the default because it is strong on the current 16-spoke
surface without becoming an every-other-spoke flicker.

`Vortex` is an effect-layer rotating spiral field. It combines logical angle,
strip radius, elapsed phase, `arms`, and `twist` into a broad sine band. Keep
`arms` low on the current 16-spoke surface; the default `3` is intentionally
asymmetric enough to avoid a fixed quarter-turn repeat.

`Shutter` is a transient folded-aperture effect. It folds logical angle into
`segments`, draws the bright blade edge around `openPct`, and can animate
openness with `mode`. The command name is `shutter`; `Iris` is not used for
this simple effect family.

`OrbitRing` is an effect-layer radial-band orbit. It gates pixels by a
strip-local `radius` and `radialWidth`, then draws one or more angular comet
lobes with optional trailing wake. When `trail` is nonzero, the angular comet
uses a directional envelope rather than a symmetric head plus a separate wake,
which avoids split sweep heads on the 16-spoke surface. Trail pixels can use
deterministic per-LED sparkle for brightness and hue jitter, and the radial
center can drift outward or inward over the animation lifetime.

`Lighthouse` is an effect-layer rotating beam. It is related to the bring-up
spoke sweep but tuned as a show effect: broad angular head, optional spoke
trail, and optional inner/outer ring bounds. The effective beam width is
clamped to at least three spokes on the current 16-spoke surface so narrow
settings do not split into separate sweep heads.

`Cymatic` is an effect-layer wave-interference field using a small fixed set of
virtual sources. It uses approximate Cartesian distance from the annular field
coordinates and low-cost sine bands; `sourceMode` selects opposite, triangular,
square, or rotating-pair source layouts.

`BreathingRings` is a transient full-surface ring field. It repeats broad
strip-local radial bands with configurable spacing and width, and can expand,
contract, or breathe in place through `direction`.

`RadialCurtain` is an effect-layer slanted front. It scans along the radial
strip like a broad curtain and adds spoke/angle phase offsets so the front
leans rather than matching the existing straight sine trace.

`PolarLattice` is a transient crossed-field pattern. It mixes radial and
angular standing waves, then applies contrast so the result can read as rings,
spokes, or a lattice depending on `mix`. The angular standing wave stays phase
stable while `speed` moves the radial phase through it, avoiding a rotating
interference read.

`Bolt` is an effect-layer deterministic jagged path. It selects a seed spoke,
then applies bounded hash-derived segment offsets and a long bend along the
radial direction, with optional forks and glow width. `seed` selects repeatable
variants.

`Sinelon` is an effect-layer bouncing sine-position head. It can start from the
inner or outer edge, limit its travel depth, attenuate later edge-to-edge
bounces, and apply a spoke-to-spoke gain wave. It renders only the current head
lobe and relies on the layer's persistent decay for its wake, so repeated bright
positions linger without requiring animation-private frame history.

`SineWave` is an effect-layer sine trace. It scans a single sine-valued head
from an edge into the strip once over `durationMs` and renders the
already-scanned history directly, so the wave shape does not depend on layer
persistence. Its trace decay is value-based per ring behind the head, and
`peakHold` can reduce that decay for bright sine peaks so slopes disappear
faster than crests. Its command lifetime is separate from scan duration and can
be omitted to use the projected time for the last peak to fall to the value
floor.

`DiagnosticFill` is a debug-layer fill used for command/output validation.

`SpokeSweep` is a debug-layer bring-up animation. Master starts it after boot to
walk logical spokes and mark radial direction, so mapping errors are visible
without giving `Display` or the output backend any animation-specific API.
It is also available manually through `/anim sweep`; use `trailSpokes=0` when
checking physical spoke order.

`SpectralWeave` is a polar spectral weave field on the `Fft` layer by default
and can also be launched on `FftAlt` for crossfades. Stage the hidden layer at
opacity `0`, launch the alternate FFT there, then use
`/layer swap Fft FftAlt <durationMs>` to fade locally on each GPU with one
PubSub command. It uses the annular
coordinate helpers plus the engine's audio controls: smoothed bass/mid/high
values modulate radial, angular, and high-frequency fields, while peak events
add restrained attack accents. Hue is driven by both absolute band energy and
smoothed spectral balance so similar loudness with different bass/mid/high
content can produce different colors without sampling raw one-frame FFT bins.
`hueModulation`
scales the allowed hue span. It renders a fallback field when no audio input has
arrived yet. It does not need full-frame rendering.

`SpectralIris` is an FFT-layer aperture and petal field. Bass opens the radial
aperture, mid strengthens the folded petal mask, high brightens the rim, and
spectral balance shifts hue. It renders the same logical pixels as
`SpectralWeave`, uses bounded integer field math, clamps petal count to avoid
16-spoke aliasing, and renders a reduced fallback field before audio input has
arrived.

`OrbitSparks` is a sparse FFT-layer ember field. It derives deterministic spark
positions from a seed, spark index, and time phase instead of storing particle
state. Bass pushes sparks radially, mid changes angular drift, and high plus
attack values brighten the splats. Its default style is `MaxValue`, and the
persistent FFT layer decay provides trails.

`StainedCells` is a low-seed membrane field on the FFT layer. It computes a
bounded Voronoi-like distance field with wrapped angular distance and squared
integer distances, then uses bass for interior brightness and high/attack
values for borders. Seed count is clamped low because this is the most expensive
of the current FFT visuals.

`WheelIndicator` is a long-running wheel-layer animation with default request
ID `1`. It renders a small spoke group from the latest wheel position and
accepts update commands for its display parameters. If no wheel state has been
received yet, it renders at position zero so the layer is still testable.
Master starts the persistent wheel indicator only after the bring-up spoke
probe window has completed.

`RadialGauge` is a configurable 500 ms UI-layer animation. Its command carries
a value and maximum, two HSV color stops, a center-ring index, and a ring-band
width. The color stops are interpolated over all spokes and the value controls
how much of that fixed spectrum is filled. Center ring `255` means the
topology's outermost ring; an explicit index selects another LED ring. Bands
wider than one are centered on that ring and shifted at a surface edge so the
requested width is preserved.

The main dial uses a white-to-white `0..31` gauge, so level 0 lights one spoke
and level 31 lights all 32. Selecting the main menu's Battery item uses the same
stable gauge request with the power node's latest fresh `0..1000` state of
charge and a red-to-green spectrum. Replaying the request restarts the 500 ms
hold; when it expires, retained UI pixels use the layer's shared two-second
fade.

`RadialMenu` is the persistent UI-layer presentation for the held rotary
menu. It divides the surface into a configurable number of angular item slots.
Each folded item has independent bar spoke width, inner-ring depth, tip spoke
width, and additional tip-ring depth. The highlighted item interpolates to a
second independently configured unfurled shape, so radial growth and optional
angular widening do not share a parameter. A populated-item mask selects
between per-item hues and the dim-white empty-slot color.

The current main menu occupies eight positions from `-3` through `+4`. Its
folded default is `4 spokes × 2 rings` plus `2 spokes × 2 rings`; its unfurled
default is `4 spokes × 6 rings` plus `2 spokes × 2 rings`. Master replaces the
stable request on `Shown` and `Moved*`, restarting the short unfurl transition,
then stops it on `Selected`. The retained UI layer supplies the existing fade
after release before any selected action replaces it.

## Buffering And Timing

The present strobe currently runs at 100 Hz. `LedPipelineBounds::targetFps` is
also 100, giving a 10 ms work budget for the GPU's present-and-render step.
Temporal dithering is disabled by configuration for production SK9822 output;
its common 5-bit level supplies hardware brightness. The legacy FastLED backend
retains its own configurable temporal-dither behavior. Neither policy should be
changed to hide pipeline timing failures.

`PresentBufferMode` is selected at compile time:

- `None`: render and present from one frame owner
- `Double`: render and present frames are distinct
- `Triple`: render, ready, and presenting storage are available; this is the
  default
- `Quadruple`: available for future experiments with deeper buffering

The production dense renderer remains single-task and sequential. If measured
100 fps acceptance fails specifically in rendering, later work may evaluate
owned-point iteration or distributed per-layer rendering. Animation code should
keep rendering through supplied canvases, but the current implementation stays
sequential until metrics justify extra tables, stacks, barriers, and failure
modes.

The pipeline does not diagnose frame health by comparing task wake-to-task wake
spacing against 10 ms; that measurement includes ISR and scheduler jitter. The
actionable timing diagnostic is the measured LED frame step: selected-buffer
presentation plus rendering the next complete buffer. Over-budget frame work is
counted as `ledDisp.slow`, while actual missed external strobes are counted as
`ledDisp.miss`.

## Host Render Tooling

This section consolidates the former offline-render plan into the active
pipeline reference. Local animation development uses the host renderer under
`tools/led-render/`. It is a deterministic inspection tool for animation,
primitive, layer, blend, topology, and color-output traces. It is not a
replacement for hardware validation.

Objectives:

- render frames `N..M` for one animation or a small timed animation sequence
  with parameters supplied from JSON
- store output in a compact binary trace that can be streamed and analyzed
- keep animations and primitives host-compilable with minimal dependencies
- provide spatial region, per-pixel time-series, frame-delta, and
  flicker/discontinuity analysis
- provide a Python interface for rendering, trace loading, analysis, and
  playback
- reflect firmware behavior closely enough that local findings are meaningful
  before hardware validation

Boundaries:

- no ESP32 hardware, serial devices, PlatformIO upload, or monitor access is
  required
- Python must not contain a second implementation of animation behavior
- host-only convenience APIs must not leak back into animation classes
- exact FastLED color-conversion and temporal-dither fidelity is not claimed
  until an optional FastLED backend is proven against host or hardware captures
- host traces reduce the search space; they do not prove electrical LED
  behavior

Main entry points:

- `bin/led-render`: run the C++ host renderer
- `bin/led-render-build`: regenerate the host animation registry and build the
  renderer
- `bin/led-analyze`: load `.tled` traces for summary, flicker, pixel, region,
  and still-capture analysis
- `bin/led-view`: open the pygame trace viewer

The host renderer includes production animation, primitive, topology,
compositor, layer-stack, and generic color-renderer headers:

- `include/LedDisplay/Animations/*/Animation.hpp`
- `include/LedDisplay/Primitives/Canvas.hpp`
- `include/LedDisplay/detail/Compositor.hpp`
- `include/LedDisplay/detail/LayerStack.hpp`
- `include/LedDisplay/Renderers/GenericRenderer.hpp`
- `include/LedTopology/Facade.hpp`

The host runtime supplies only the environment that isolated animations need:
frame clock, hue and rotation offsets, logical-to-local mapping,
scratch/output buffers, optional FFT, peak, and wheel input snapshots,
derived audio controls, and config payloads loaded from JSON. Animations and
primitives should remain isolated enough to compile in this runtime without
services, tasks, PubSub, queues, logging, or device output dependencies. If an
animation needs broad host emulation to compile, the animation boundary is the
thing to fix.

Animation discovery is generated from `include/LedDisplay/Animations/` naming
conventions, with each animation using sibling `Animation.hpp` and `Config.hpp`
headers. Command publishers use sibling `Command.hpp` and CLI registration uses
`CommandDesc.hpp`. The host registry is emitted to
`tools/led-render/generated/AnimationRegistry.hpp`. Adding a normal animation
should not require manual host dispatch edits when the animation follows the
current per-animation directory shape. A future source annotation such as
`LED_ANIMATION` may replace directory scanning if the naming convention becomes
too weak, but a manually maintained host-only animation list should be avoided.

The renderer accepts JSON configuration rather than positional
animation-specific flags. The parser maps animation and enum names with
`magic_enum` where possible, rejects unknown fields, and rejects type
mismatches so traces remain reproducible.

Example config:

```json
{
  "animation": "CenterWave",
  "duration_ms": 1200,
  "layer": "TransientEffect",
  "hue_offset": 0,
  "rotation_offset": 0,
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

Rendering modes:

- `animation`: render one animation directly through `AnimationRenderContext`
  to test isolated animation and primitive behavior
- `pipeline`: render through scratch -> layer -> composed-frame, matching the
  firmware pipeline shape for layer policy, blend behavior, decay, and final
  HSV composition checks
- `animations: [...]`: run a small timed sequence with `start_ms`,
  `duration_ms`, `layer`, and `config` entries for overlap/regression fixtures

Trace files use the `.tled` format: `TLED` magic, versioned little-endian
header, metadata JSON, then contiguous per-frame planes. Pixel order is logical
`(spoke, radial)` unless the metadata says otherwise; logical order is the
default because it is stable across GPU ownership and easier to analyze.

Header data includes frame count, first frame index, FPS numerator and
denominator, timestamp step in microseconds, topology dimensions, logical and
local pixel counts, plane mask, per-frame record size, and metadata JSON byte
length. Current planes are:

- `hsv_final`: final composed HSV frame, three bytes per pixel
- `rgb_final`: deterministic RGB frame from the selected color backend, three
  bytes per pixel
- optional `hsv_scratch`: raw animation scratch frame
- optional `hsv_layer_<name>`: selected layer frames

Trace metadata should record the renderer command line, git commit if
available, animation name, animation config JSON, render mode, topology,
ownership mode, color backend, FastLED fidelity mode, build timestamp, and the
`requires_full_frame` trait for each rendered animation.

The current color backend is deterministic `GenericRenderer` HSV-to-RGB
conversion. Optional FastLED host rendering can be added as a fidelity mode if
FastLED host compilation is practical. The target is to reuse FastLED color
conversion and, where possible, temporal dithering behavior. Firmware behavior
remains the source of truth; host traces are diagnostic evidence, not a second
renderer to tune the firmware against.

Python is a thin control and inspection layer:

- `led_render.run_render(...)`: invoke `bin/led-render`
- `led_render.Trace`: memory-map or load binary traces
- `Trace.frames(plane="hsv_final")`
- `Trace.pixel_series(spoke, radial, plane="hsv_final")`
- `Trace.region(spokes=..., radials=..., plane=...)`
- `led_render.analysis.detect_flicker(trace, ...)`
- `led_render.analysis.detect_hue_ping_pong(trace, ...)`
- `led_render.analysis.frame_deltas(trace, ...)`
- `led_render.viewer.play(trace, ...)`

Trace loading and analysis use NumPy. Interactive playback uses pygame because
it gives direct access to the rendered pixel surface and simple real-time
controls. The tooling also includes a dependency-light `.ppm` still-frame
exporter so capture is not blocked when pygame is unavailable.

Viewer controls should include play/pause, frame step forward/backward, speed
control, frame slider, plane selector, color mode selector, optional pixel
hover/readout, optional spoke/ring grid overlay, flat heatmap view, and radial
umbrella view. The flat `(spoke x radial)` heatmap remains the primary analysis
view; the radial view is primarily for pattern design and uses the annular
geometry metadata embedded in each generated trace.

Initial analysis reports include per-frame max deltas in HSV and RGB channels,
per-pixel max delta over time, isolated spike detection, hue ping-pong
detection, value spike detection, temporal variance heatmaps, and
frame-to-frame discontinuity reports with the top offending pixels. Analysis
thresholds must be configurable because useful sensitivity depends on the
animation and plane under inspection.

Build integration is intentionally simple:

```sh
bin/led-render-build
```

The wrapper compiles with C++23, includes `include/`, supplies only host
topology/ownership defaults, and avoids PlatformIO or ESP-IDF include paths.
If direct compilation exposes embedded dependencies, fix the offending
animation or primitive boundary first. Add heavier build-system integration
only if the direct wrapper becomes hard to maintain.

Checked-in sample configs live under `tools/led-render/examples/`:

- `center-wave-green.json`
- `center-wave-purple.json`
- `center-wave-green-purple-adjacent.json`
- `wheel-indicator-static.json`
- `spoke-sweep.json`
- `spectral-weave-static.json`
- `spectral-weave-polar-static.json`
- `spectral-weave-audio-sweep.json`
- `spectral-iris.json`
- `orbit-sparks.json`
- `stained-cells.json`

Current implementation status:

- generated animation discovery and dispatch are in place
- all discovered animations compile in the host translation unit
- the C++ renderer CLI consumes generated/table-driven JSON parsing
- `hsv_final` and `rgb_final` traces are emitted in logical topology order
- synthetic audio timelines and peak input snapshots are supported for
  audio-reactive fixtures
- full-frame animation traits are emitted in metadata and followed by the host
  render path
- Python can load traces, summarize them, extract pixels and regions, report
  frame deltas, detect flicker-like discontinuities, play traces, and export
  still frames
- pygame playback supports frame indicators, heatmap viewing, radial viewing,
  LED spacing, circular LEDs, preview brightness, and bloom
- the remaining fidelity extension is optional FastLED host color conversion
  and temporal-dither support

Common commands:

```sh
bin/led-render \
  --config tools/led-render/examples/center-wave-green.json \
  --output /tmp/center.tled

bin/led-analyze summary /tmp/center.tled --stats
bin/led-analyze pixel /tmp/center.tled --spoke 3 --radial 12
bin/led-analyze region /tmp/center.tled --spokes 0:4 --radials 10:20
bin/led-analyze flicker /tmp/center.tled --plane hsv_final
bin/led-view /tmp/center.tled --bloom
```

## Metrics

LED display metrics are in the `ledDisp` group:

`LedDisplay` prewarms `ledDisp` metrics during component begin, before the
render task or PubSub input callbacks can run. Keep that ordering intact:
render, strobe, wheel, and FFT paths must treat `metrics()` as a hot-path
accessor only. Lazy metric registration in those paths can hit C++ static guard
initialization across cores and trigger interrupt-watchdog failures.

- `qFail`: engine command queue failures
- `badCmd`: invalid or undecodable commands
- `rndFail`: render failures
- `showFail`: output presentation failures
- `inFail`: FFT/wheel input decode failures
- `cmd`: accepted animation commands
- `play`: play commands handled
- `update`: update commands handled
- `updMiss`: updates with no matching active animation
- `stop`: stop commands handled
- `fftIn`: FFT input frames captured
- `whlIn`: wheel input states captured
- `repeat`: repeated presents because no newer frame was ready
- `miss`: missed present strobes
- `slow`: frame steps whose measured work exceeded the 10 ms budget
- `active`: active animation slot count
- `rndMax`: maximum observed render duration in microseconds
- `showMax`: maximum observed output-show duration in microseconds
- `encMax`: maximum observed SK9822 HSV-to-wire encoding duration in
  microseconds
- `spiQMax`: maximum observed SPI3 transaction queue duration in microseconds
- `spiWMax`: maximum observed SPI3 completion-wait duration in microseconds
- `stepMax`: maximum observed present-and-render step duration in microseconds

Validation on 2026-05-25 with `master`, `gpu0`, `gpu1`, and `io` attached
showed stable operation after the metrics prewarm fix. User observation
confirmed the spoke mapping and the bring-up-to-normal-operation gate. Both
GPUs received FFT-driven animation traffic and the forced
`/anim wheel-update 160 96 3 1 1` update through master.

The 2026-05-25 12:45 metrics snapshot showed `qFail=0`, `badCmd=0`,
`rndFail=0`, `showFail=0`, `inFail=0`, `repeat=0`, `miss=0`, and `slow=0` on
both GPUs. The forced wheel update incremented `update` without increasing
`updMiss`, proving that the persistent wheel indicator received the generic
update path. Observed maxima were:

- `gpu0`: `rndMax=3312us`, `showMax=2244us`, `stepMax=4652us`
- `gpu1`: `rndMax=3725us`, `showMax=3117us`, `stepMax=4765us`

These are comfortably below the 8 ms work budget for the legacy LED count.
They are a historical baseline, not evidence for the 32×60 SK9822 build; the
production-code physical bench must establish new maxima.
Separate master-side PubSub SPI RX queue backpressure warnings were seen during
the same monitor session; those are transport pressure signals, not LED render
pipeline failures.

## Validation Commands

Run host LED protocol/topology and production ownership regressions:

```sh
bin/test-led-display
bin/test-led-render-stitch
```

Build all active environments:

```sh
bin/build -e master
bin/build -e media
bin/build -e gpu0
bin/build -e gpu1
bin/build -e io
```

Upload one environment at a time, then attach the monitor:

```sh
bin/build -e master -t upload
bin/build -e gpu0 -t upload
bin/build -e gpu1 -t upload
bin/build -e media -t upload
bin/build -e io -t upload
bin/monitor-multi --strip-ansi master gpu0 gpu1 io
```

Sample metrics and force the wheel update path:

```text
!gpu0 /metrics
!gpu1 /metrics
!master /anim wheel-update 160 96 3 1 1
!gpu0 /metrics
!gpu1 /metrics
```
