# Animation Pipeline Port Plan

This is the current implementation plan for porting the useful legacy animation
pipeline into the current repository. It is intentionally a plan, not an
implementation contract. The open questions at the end need resolution before
coding the full pipeline.

## Goals

- Match the current repo structure and style.
- Keep the render and LED show paths allocation-free after static startup.
- Keep animation requests decoupled from animation execution.
- Preserve topology-aware animations, starting with the umbrella topology.
- Let each GPU node compute and render only the LED group it owns.
- Keep topology fixed at compile time for each firmware image.
- Treat FastLED as a replaceable LED-library backend, not as the whole platform
  abstraction.
- Put ESP32 RMT choices behind a separate ESP32 platform policy because RMT is
  critical on ESP32 but not portable to other targets.
- Make tuning knobs compile-time configurable through `StaticConfig`, following
  the same style as the FFT configuration.
- Use FastLED global brightness and temporal dithering for low-brightness
  output, while keeping internal frame buffers at full 8-bit range.
- Keep hot paths predictable: fixed storage, no dynamic polymorphism, no string
  lookup, no heap allocation, no per-frame sorting.

## Decisions From Clarifications

- The old websocket LED mirror is discarded for GPU nodes. A later web
  interface belongs on the master or a host-side tool. GPU-to-master frame
  readback for visualization can be evaluated later as telemetry, but it is not
  part of the render pipeline.
- Production-looking legacy animations should not be preserved visually. Keep
  the useful pipeline concepts and rewrite the diagnostic ring, spoke, strip,
  and index patterns.
- Current bring-up should target 100 FPS.
- The current free-running 100 FPS render cadence is bring-up scaffolding. The
  production cadence should be driven by a master-generated GPIO present
  strobe.
- The present strobe is a presentation boundary, not a render start. GPU nodes
  should render ahead where possible, then present the newest complete frame on
  the strobe edge.
- Current two-node GPU setup uses equal groups and two data lines per node.
- GPIO1 and GPIO2 are the initial output pins for each GPU node.
- GPU nodes receive animation requests and state/events over PubSub. Boot
  animations are also requested over PubSub after the master has completed the
  node handshake.
- `LED_GROUP_COUNT` and `LED_GROUP_INDEX` select the equal-sized node-owned LED
  group. LED count is assumed to be divisible by group count.
- A separate compile-time data-line configuration selects how many output lines
  exist on the node. The owned group is split into equal sequential line spans.
- Animation command payloads should carry an opaque `std::byte` array. Each
  animation owns a small config struct that is decoded from that byte payload
  with the existing PubSub SerDe.
- Physical LED strips do not need to be connected for phases that validate
  scheduling, memory use, command handling, FastLED/RMT timing, and frame
  budget. The GPU nodes can run as if LEDs were connected.
- PubSub integration can be deferred during early runtime bring-up. A local
  command/test hook is acceptable until the LED pipeline itself is stable.
- LED rendering should be isolated to core 1. Supplemental work should default
  to core 0 unless it is tightly coupled to render or present timing.

## Non-Blocking Assumptions For This Plan

- The production component can be named `LedDisplay`, with topology in a
  separate `LedTopology` component.
- The first implementation targets `env:gpu0` and `env:gpu1`, while keeping the
  compile-time model ready for four GPU nodes.
- The umbrella topology keeps the legacy physical hardware sequence:
  736 LEDs, 4 strips, 184 LEDs per strip, 4 segments per strip, 46 LEDs per
  segment, 16 logical spokes, and 46 logical rings.
- Animation state should be owned by the GPU render task. Other components only
  enqueue requests or update typed latest-state slots.
- The internal public color structs should stay small project-local POD types.
  FastLED types may be used inside the FastLED backend and FastLED operation
  adapter.

## Proposed Component Layout

Use the same boundary shape described in `docs/structure.md`.

### `include/LedTopology/`

- `Facade.hpp`: exports public topology types and umbrella topology.
- `Interfaces/Types.hpp`: `LogicalPixelIndex`, `PhysicalPixelIndex`,
  `LocalPixelIndex`, topology feature tags, and small view structs.
- `Interfaces/Config.hpp`: only if runtime-facing topology config is needed.
- `detail/Umbrella.hpp`: compile-time umbrella topology tables and helpers.
- `detail/OwnedPixels.hpp`: compile-time LED group ownership helpers.

The topology component should not know about FastLED, RMT, tasks, or PubSub.

### `include/LedDisplay/`

- `Facade.hpp`: exports the display component and request/types needed by
  callers.
- `Interfaces/Config.hpp`: task config, frame rate, brightness, output config,
  layer config, and animation slot config.
- `Interfaces/Types.hpp`: color type, blend op, layer enum, lifetime, animation
  request payloads, and typed event/state input types.
- `detail/Display.hpp`: lifecycle/task owner following the `LedPwm` and PubSub
  component pattern.
- `detail/Pipeline.hpp`: request service, render, compose, and present flow.
- `detail/Layer.hpp`: fixed layer buffers, decay, and blend settings.
- `detail/AnimationSlot.hpp`: fixed active animation slots and generation
  handles.
- `detail/Animations/*.hpp`: concrete animation classes.
- `detail/Blend.hpp`: hot blend functions.
- `detail/Primitives.hpp`: reusable topology drawing helpers for rings,
  spokes, segments, strips, gradients, pulses, sweeps, trails, and masks.
- `detail/Renderer.hpp`: tiny templated passthrough wrapper around the selected
  renderer implementation.
- `detail/RendererSelect.hpp`: compile-time renderer selection.
- `Renderers/GenericRenderer.hpp`: generic no-FastLED implementation of
  low-level rendering operations.
- `Renderers/FastLedRenderer.hpp`: FastLED-backed implementation of low-level
  rendering operations.
- `detail/output/OutputSelect.hpp`: compile-time LED-library output selection.
- `Outputs/FastLedOutput.hpp`: FastLED output backend.
- `detail/platform/esp32/RmtPolicy.hpp`: ESP32 RMT and optional fallback policy.

Pipeline and animation headers should not include `<FastLED.h>` directly. They
call project-local APIs. The FastLED renderer and FastLED output backend own
FastLED-specific includes.

The renderer selection should follow the same repository style as concrete
transports and outputs: expose concrete implementations in a named folder and
select one concrete type at compile time. There should be no virtual dispatch in
rendering. The only templated layer should be a small passthrough wrapper such
as `Renderer<SelectedRenderer>`, and the larger pipeline classes should remain
non-templated so clangd keeps useful diagnostics.

### `include/StaticConfig/LedDisplay.hpp`

Static bounds and tuning knobs should live here, following
`StaticConfig/LedPwm.hpp` and the FFT configuration style:

- topology selection, initially `Umbrella`
- umbrella constants and compile-time validation
- LED chipset and color order
- output backend selection, initially `FastLED`
- renderer selection, initially `FastLED` where useful and `Generic`
  otherwise
- target FPS, initially 100
- global brightness default
- temporal dithering enablement
- color correction and color temperature defaults
- optional power limit defaults
- `ledGroupCount`
- `ledGroupIndex`
- `dataLineCount`
- per-data-line pin list, initially GPIO1 and GPIO2
- per-data-line span count derived from equal sequential splitting
- max layers
- max active animations
- command queue size
- layer clear, decay, opacity, and blend defaults
- animation-specific tuning defaults
- optional profiling switches, compiled out by default
- ESP32 RMT policy knobs when building on ESP32

Old platform `#define` settings should be reflected here where they describe
the LED setup. That includes GPIOs, output line count, color order, chipset,
brightness cap, and relevant driver policy.

## PlatformIO Configuration

Add build flags to GPU environments:

- `env:gpu0`: `-DLED_GROUP_COUNT=2`, `-DLED_GROUP_INDEX=0` initially
- `env:gpu1`: `-DLED_GROUP_COUNT=2`, `-DLED_GROUP_INDEX=1` initially

When moving to four nodes, add `gpu2` and `gpu3` environments and set indices
0..3. The future data-line split needs one additional compile-time profile or
mapping selector, because node ownership and physical output line count are not
the same concern.

FastLED remains a GPU-only dependency for the initial output backend. Other
environments should not gain the dependency unless they instantiate the backend.

## Core Data Model

### Color

Start with local byte-sized color types:

- `HsvColor { uint8_t hue, saturation, value }`
- `RgbColor { uint8_t red, green, blue }`, only if needed outside the output
  backend

Most animation code should render HSV because hue rotation, value trails,
palettes, and diagnostics are natural in HSV. Conversion to RGB should happen
late, ideally inside the output backend.

Do not globally pre-scale every pixel for brightness. Keep the frame at full
0..255 dynamic range and pass global brightness to FastLED so temporal
dithering can preserve low-range motion.

### Renderer Selection

Use a small static renderer abstraction for low-level hot operations:

- `GenericRenderer`: manual constexpr-friendly implementations for scale,
  saturating add, blend, fade, HSV/RGB conversion, and palette lookup where
  needed.
- `FastLedRenderer`: forwards to FastLED optimized helpers when they map cleanly,
  such as `scale8`, `scale8_video`, `qadd8`, `nblend`, `blend`, `nscale8`,
  `fadeToBlackBy`, `fill_solid`, HSV conversion, palettes, noise, and useful
  wave functions.

The selection should be static:

```cpp
using SelectedRenderer = Renderers::FastLedRenderer;
using Renderer = detail::Renderer<SelectedRenderer>;
```

`detail::Renderer<T>` should be pure passthrough and small enough that clangd
breakage in templates does not hide real implementation diagnostics. Rendering
algorithms, layers, animation slots, and concrete animations should stay
non-templated and call `Renderer::blend(...)`, `Renderer::fade(...)`,
`Renderer::hsvToRgb(...)`, and similar project-local names.

### Topology

Animations render into logical topology space. The umbrella topology maps those
logical coordinates to the physical LED order used by the WS2812B data lines.

The physical umbrella layout is:

- 4 physical strips
- each strip has 4 segments
- segment orientation alternates
- each strip is one contiguous WS2812B signal line
- total LEDs: 4 strips * 4 segments * 46 LEDs = 736 LEDs

The logical views are:

- 16 spokes, one per physical segment
- 46 rings, one per radial offset across all spokes
- optional strip, segment, and endpoint views for diagnostics

The mapping for one strip is serpentine:

```text
segment 0: inner -> outer
segment 1: outer -> inner
segment 2: inner -> outer
segment 3: outer -> inner
```

This preserves the legacy index behavior:

```cpp
strip = spoke / 4;
segment = spoke % 4;
segmentBase = segment * ledsPerSegment;
position = (segment % 2 == 0)
    ? radialIndex
    : (ledsPerSegment - 1 - radialIndex);
physical = strip * ledsPerStrip + segmentBase + position;
```

Topologies should expose common feature views, but animations should be allowed
to be conditionally available when a topology lacks a feature.

### LED Group Ownership And Local Buffers

Group ownership is fixed at compile time:

```cpp
static constexpr bool owns(PhysicalPixelIndex pixel);
static constexpr LocalPixelIndex localIndex(PhysicalPixelIndex pixel);
static constexpr bool ownsLogical(LogicalPixelIndex pixel);
```

The output buffer on each node should be compact and local. If a node owns
global physical LEDs 368..735, local physical LED 368 must map to output buffer
index 0. FastLED should render directly from that compact buffer without gaps.

For effects that naturally iterate physical pixels, prefer iterating local owned
pixels directly. For effects that iterate rings, spokes, or strips, keep the
topology loop and let the constexpr ownership/local-index layer skip non-owned
pixels:

```cpp
if constexpr (LedDisplayConfig::ledGroupCount > 1) {
    if (!ctx.owns(pixel)) {
        continue;
    }
}
ctx.write(pixel, color);
```

The write helper should handle global-to-local translation so animation code
does not manually subtract offsets.

### Data Lines

Node ownership and output data lines are separate compile-time concerns:

- `LED_GROUP_COUNT` and `LED_GROUP_INDEX` select the equal-sized logical
  ownership group.
- `dataLineCount` selects how many LED library controllers/RMT devices are
  instantiated on that node.
- `ownedPixelCount = totalPixels / ledGroupCount`.
- `linePixelCount = ownedPixelCount / dataLineCount`.
- static validation requires both divisions to be exact.
- per-line descriptors map each local compact sequential span to a GPIO pin.

For the current two-node setup, each GPU node owns two complete physical strips
and exposes two data lines. Each line maps to one contiguous strip sequence.
The initial per-node pins are GPIO1 and GPIO2.

For example, with 400 total LEDs, two GPU nodes, and two data lines per node:

- node 0 owns 0..199, line 0 drives 0..99, line 1 drives 100..199
- node 1 owns 200..399, line 0 drives 200..299, line 1 drives 300..399

If the same node-owned range is split across four data lines, each line drives
50 sequential LEDs. This assumes the physical wiring follows the configured
sequential split.

### Buffers

Use fixed storage. A practical first shape is:

- one scratch buffer sized to the current node's owned LED count
- one local HSV buffer per layer
- one local final HSV composed buffer
- one local FastLED `CRGB` output buffer per data line or one compact aggregate
  buffer with line spans, depending on what the backend supports best

Local-indexed buffers avoid storing pixels the node will never render. The
topology/write layer absorbs global-to-local translation.

All clearing, decay, blend, compose, HSV/RGB conversion, and output loops must
iterate local owned pixels only.

## Request And Animation Ownership

Use a static queue like `LedPwm`:

- queue item is trivially copyable
- storage is created with `Totem::Queue::Platform::create`
- display task drains requests at the start of each frame
- publish path wakes the display task if `useNotify` is enabled

Do not port `std::function`, `std::unique_ptr`, or heap-allocated requests.

Represent active animations as a bounded variant. Keep the initial variant very
small and expand it only when a later phase needs the new effect:

```cpp
using AnimationPayload =
    std::variant<DiagnosticFill, FftReactive, CenterWave>;
```

Each active slot stores:

- active flag
- generation
- target layer
- lifetime policy
- start time
- animation payload variant

Handles should include slot index plus generation. Detaching an animation must
not move other active slots.

Later phases can add diagnostic rings/spokes, wheel indicators, beat pulses,
palette backgrounds, and production effects by extending the variant. Do not
front-load the full animation catalog.

## PubSub Control Model

GPU nodes should remain pure renderers. They receive scene, animation, and event
state over PubSub. They should not own user-facing web endpoints.

Recommended control split:

- The master should be authoritative for scene selection and animation playback
  start commands. For example, a bell event can make the master send
  "play explosion" or "play center wave" with parameters.
- Fire-and-forget animations should carry their lifetime in the playback
  command and expire on the GPU node.
- Continuous animations should be started once. Follow-up FFT, beat, or wheel
  payloads should update typed latest-state slots without dequeueing and
  requeueing the animation.
- High-rate events such as FFT frames should be compact state messages, not a
  stream of animation spawn commands.
- Boot animations should be requested by the master after all GPU nodes have
  completed handshake, so background layers can initialize simultaneously.

This gives the master control over behavior while avoiding command overhead and
jitter in the 100 FPS render path.

Keep animation playback commands under one PubSub topic if possible. The
initial simple shape should be a generated fixed-size wire message:

```cpp
enum class AnimationCommandType : uint8_t { Play, Stop, SetParam };
enum class AnimationKind : uint8_t { DiagnosticFill, FftReactive, CenterWave };

struct AnimationCommand {
    AnimationCommandType type;
    AnimationKind kind;
    uint16_t requestId;
    uint8_t layer;
    uint16_t lifetimeMs;
    uint8_t payloadSize;
    std::array<std::byte, animationCommandPayloadBytes> payload;
};
```

The master can expose typed helpers such as `playCenterWave(...)`, while the
wire schema stays one topic and one fixed message. Each helper encodes an
animation-specific config struct into `payload` with the existing PubSub SerDe.
The GPU command handler validates `payloadSize` against the expected encoded
size for `kind` and decodes the config when servicing the command, not in the
render loop.

This keeps the command topic honest: the wire payload is opaque bytes, and the
type-specific meaning lives with the animation. Spawn commands are low
frequency enough that the SerDe decode overhead is acceptable.

## Task Model

Start simple: one render runner should own frame rendering and `show()`:

1. drain animation request queue
2. consume latest PubSub/event state slots
3. decay/clear owned layer pixels
4. render active animations into scratch
5. blend scratch into target layers
6. remove expired animations
7. compose layers into final HSV
8. ask the output backend to convert and show owned pixels

This intentionally does not port the old display-task plus driver-task inbox
split. The new distributed hardware reduces the original interrupt conflict,
and a single owner render runner avoids frame queue phase problems and copies.

Use `HasLifecycle` and `HasTaskController`, matching components such as
`LedPwm`, `Buttons`, and PubSub. The task config should default to notify
wakeups and no catch-up, with a periodic interval matching the target 100 FPS.

After the render runner is working, additional runners are acceptable because
GPU nodes have enough memory and few unrelated duties. Useful later runners:

- command/event ingestion if PubSub callbacks should stay tiny
- low-priority pruning or maintenance for expired animation slots
- profiling/metric reporting outside the render runner

Do not add extra queues or tasks before there is a concrete reason in the
current phase.

## FastLED Output Backend

`LedDisplay/detail/output/FastLED.hpp` should own all LED-library output code
for the first implementation:

- include `<FastLED.h>`
- configure WS2812B controllers
- hold or reference compact FastLED `CRGB` output arrays
- convert local HSV output to RGB if conversion is not delegated to
  `FastLedRenderer`
- apply global brightness with `FastLED.setBrightness()` or
  `FastLED.show(brightness)`
- enable FastLED temporal dithering
- call `FastLED.show()`
- report backend errors through `ReturnCode`

The public display and animation code should call methods such as:

```cpp
ReturnCode init(const OutputConfig &config, const DisplayFacts &facts);
ReturnCode show(std::span<const HsvColor> frame,
                std::span<const DataLineSpan> lines);
ReturnCode setBrightness(uint8_t brightness);
ReturnCode deinit();
```

The output backend is library-specific. A future non-FastLED library can add a
new output backend without changing topology or animation code. Like the
renderer selection, output selection should be compile-time and concrete, not
virtually dispatched.

## Present Strobe Timing

The production display cadence should be event-driven. The master owns the
frame clock and drives a GPIO present strobe from a hardware timer at the
target display cadence. GPU nodes treat that edge as permission to present the
newest complete frame, not as a request to start rendering that frame.

The GPU-side split should be:

- a render task pinned to core 1 owns animation stepping, layer composition,
  and writable frame buffers
- a GPIO ISR timestamps the strobe edge and signals a small present task
- the present task flips or selects the newest complete frame and calls the
  output backend
- support tasks such as PubSub ingestion, expiration pruning, and metric export
  run on core 0 unless measurements show they must be co-scheduled with render

If the render task has not completed a new frame by the strobe edge, the present
task should repeat the last complete frame and record a missed-present metric.
It should not present a partially composed or late frame. Animation commands
that need synchronized start across GPU nodes can later carry a target master
strobe counter such as `startFrame`.

On the current FastLED ESP-IDF 5 RMT path, `FastLED.show()` queues asynchronous
RMT refresh work and normally returns before the WS2812B wire transfer has
finished. It still performs synchronous pixel loading, brightness/dither work,
and controller calls, and it can wait if the previous RMT refresh is still in
flight. Treat that wait as present backpressure, not as the normal wire-time
cost of every frame.

The initial software timer cadence should remain as a disconnected bring-up
fallback until the master strobe output and GPU strobe input are implemented.
After that, the software cadence is only a diagnostic fallback, not the
production timing model.

## ESP32 RMT Policy

`LedDisplay/detail/platform/esp32/RmtPolicy.hpp` should describe ESP32-specific
output policy selected by static config. It should stay below the FastLED
backend rather than becoming the public LED backend.

Policy knobs to expose through `StaticConfig` or build flags:

- use FastLED's ESP32 RMT output path by default
- RMT interrupt priority when supported
- DMA mode when supported, with ESP32-S3 caveats measured before enabling DMA
- RMT memory/block settings where FastLED exposes them
- optional I2S or SPI clockless output fallback for experiments
- raw pin-order define if required by the selected ESP32 board mapping
- minimal error logging, compiled out of hot loops

FastLED's ESP32 source currently routes WS2812B output through its ESP32 RMT
controllers when available. The first implementation should use that path and
make the policy explicit. FastLED RMT defaults are acceptable for the first
implementation. A custom ESP-IDF RMT backend or lower-level RMT tuning should
only be added if measurements show FastLED's backend cannot meet the
frame-budget or stability requirements.

## Brightness And Dithering

Use FastLED's global brightness path for total output brightness:

- keep internal HSV/RGB values at full 8-bit range
- set global brightness in the FastLED backend
- enable binary temporal dithering
- avoid per-pixel global brightness scaling before `show()`
- measure actual FPS because FastLED's own code disables dithering when its
  measured FPS falls below its threshold

The target 100 FPS is therefore part of visual correctness, not just a
performance preference. If the render path falls below the dither threshold,
low-brightness smoothness will regress.

When dithering is enabled, a measured FPS below the FastLED dither threshold
must be treated as an error-level event. Emit it through the normal logging and
metrics path with rate limiting so the render loop is not flooded. If FastLED
does not expose its internal measured FPS, use the display runner's measured
frame cadence as the source of truth.

Per-layer opacity and animation opacity remain compositing controls. They should
not replace the final global brightness cap.

## Rendering Primitives To Add Later

The old implementation had useful topology concepts but very few reusable
visual primitives. Add a small primitives layer after the LED infrastructure is
working and before production effects become the main focus:

- set/fill pixel, local span, strip, segment, spoke, and ring
- ring and spoke sweeps
- palette and gradient lookup
- radial and angular masks
- value fade and decay helpers
- pulse/envelope helpers with integer or fixed-point math
- trail helpers using precomputed scales, not per-frame `powf()`
- topology-aware diagnostic markers
- optional noise and wave helpers through `FastLedRenderer`

Keep primitives thin and concrete. They should remove repeated hot-loop logic,
not become a runtime scene graph.

## Phased Implementation Plan

This should proceed like the SPI work: small, reviewable phases that each prove
one layer of the system. Do not implement all abstractions up front.

Current status: the initial GPU implementation covers phases 1 through 5. It
adds `LedTopology`, `LedDisplay`, compile-time group/data-line ownership,
FastLED-backed output, a temporary free-running 100 FPS render/present task,
fixed animation slots, opaque byte-backed animation config payloads, and a
local `/ledwave` command that queues the default center-to-edge wave on each GPU
node. The implementation also contains an early `FftReactiveConfig` payload and
render placeholder, but the real FFT latest-state subscriber remains Phase 6
work.

### Phase 1: Skeleton

- Add `LedTopology`, `LedDisplay`, and `StaticConfig/LedDisplay.hpp` skeletons
  following existing component structure.
- Add compile-time config fields for topology, group count/index, data-line
  count, GPIO1/GPIO2 pins, target FPS, brightness, and backend selections.
- Add PlatformIO build flags for `LED_GROUP_COUNT` and `LED_GROUP_INDEX`.
- Add empty lifecycle/task wiring that compiles for `gpu0` and `gpu1`.
- No FastLED output, no animations, no PubSub schema changes beyond reserved
  names if needed.

### Phase 2: Topology And Ownership

- Implement umbrella logical-to-physical mapping.
- Implement compile-time group ownership and local index translation.
- Implement equal sequential data-line splitting within each owned group.
- Add compile-time validation for divisibility and line spans.
- Verify with compile checks and simple code inspection diagnostics.

### Phase 3: FastLED Static Output

- Add the FastLED output backend with compact local buffers.
- Use GPIO1/GPIO2 and FastLED RMT defaults.
- Add global brightness through FastLED and enable temporal dithering.
- Render static fills by group and by data line.
- Add error-level reporting for measured FPS below the dither threshold.

### Phase 4: Minimal Render Runner

- Add the temporary free-running render/present runner at 100 FPS.
- Add local frame buffers and the selected renderer passthrough.
- Add the smallest useful clear/fill/blend path.
- Add basic frame-time metrics and missed-budget reporting.
- Mark this cadence as bring-up scaffolding that will be replaced by the
  master-driven present strobe.
- Keep animation slots and PubSub playback out of this phase unless required
  for the static output test.

### Phase 5: Playback Command Skeleton

- Add the fixed-size `AnimationCommand` payload shape with opaque byte config
  storage.
- Add generated wire config structs for the first trivial diagnostic animation
  and center-to-edge wave.
- Add SerDe encode/decode helpers for animation configs.
- Add the GPU-side request queue and fixed animation slots.
- Add `Play`, `Stop`, and lifetime expiration for one trivial diagnostic
  animation.
- Add a local command/test hook to issue one command without relying on PubSub.
- Add the real PubSub `Animation` topic here only if PubSub tuning is no longer
  blocking; otherwise keep the hook local until Phase 7.

### Phase 6: First Practical Visual Test

- Add one parameterized persistent animation that consumes latest FFT data.
- Add one master-triggered fire-and-forget animation, initially a center-to-edge
  wave.
- Start the persistent animation once, then feed FFT updates through latest-state
  slots without respawning it. Synthetic/local FFT data is acceptable in this
  phase if PubSub is not ready.
- Trigger the wave through the local hook or the master command path, depending
  on which is ready, and let it expire on the GPU.
- Verify both effects at 100 FPS with dithering still active.

### Phase 7: Present Strobe, PubSub Integration, Layers, And Maintenance

- Add the master hardware-timer GPIO present output and the GPU GPIO strobe
  input behind `StaticConfig`.
- Split render and present responsibilities so render runs ahead on core 1 and
  the strobe-triggered present path only flips/selects complete frames and
  starts FastLED/RMT output.
- Keep the software timer cadence as a diagnostic fallback for disconnected
  bring-up.
- Add strobe counters, optional target `startFrame` scheduling, and repeated
  frame behavior for missed render deadlines.
- Add the single `Animation` PubSub topic if it was not added in Phase 5.
- Subscribe GPU nodes to FFT, beat, wheel, and animation topics as their wire
  payloads stabilize.
- Replace local synthetic/latest-state hooks with PubSub-fed latest-state slots.
- Add the fixed layer stack and layer composition.
- Add layer opacity, blend mode, clear/decay policy, and generation handles.
- Add a lower-priority maintenance runner only if pruning or metrics are better
  decoupled from the render runner.
- Preserve single ownership per hot-path role: render owns writable frame state,
  and present owns output handoff. The present path must not render.

### Phase 8: Diagnostics And Additional Inputs

- Add ring, spoke, strip, endpoint, and index diagnostics.
- Add wheel and beat latest-state subscribers when those wire payloads are ready.
- Add boot-scene playback after master/GPU handshake.
- Keep websocket and GPU-hosted web interfaces out of scope.

### Phase 9: Performance And Backend Tuning

- Replace generic hot operations with `FastLedRenderer` calls where they are
  measurable wins.
- Tune buffer layout, line spans, and loop order based on profiling.
- Evaluate lower-level RMT knobs only if FastLED defaults miss the target.
- Validate four-node and four-data-line builds when wiring exists.

### Phase 10: Visual Effects Iteration

- Repeatably add and tune production effects.
- Grow the primitives layer only where it removes real duplication.
- Add animation-specific typed helpers on the master side while keeping GPU
  payloads fixed-size and allocation-free.
- Keep each effect addition separately reviewable.

Do not port `AnimSparkle`, `AnimWhiteDots`, or the old visually intended
animations as-is. Reuse only their useful debug intent or pipeline concepts.

## Metrics And Logging

Add a new log component and metric component only when the implementation starts
touching code, not in this planning-only step.

Useful metric groups for the implementation phase:

- core: queue failures, render failures, backend show failures
- display: frames rendered, requests handled, active animation count
- profiling: render microseconds, compose microseconds, present latency
  microseconds, show-call microseconds, missed frame budget
- strobe: strobe interval, strobe counter, repeated frames, missed presents,
  previous RMT refresh still in flight
- error: dither enabled but measured FPS below FastLED's dither threshold

Keep per-pixel or per-layer profiling compiled out by default. Hot render loops
must not emit logs.

## Verification Plan

Verification should stay phase-specific because there is no host-side test
harness.

- Phase 1: build `gpu0` and `gpu1`; build other environments only if shared
  headers or build flags require it.
- Phase 2: build both GPU environments and inspect/static-check topology,
  ownership, and data-line spans.
- Phase 3: run GPU firmware on devices, even with no physical LED strips
  connected, and verify static fills by group/data line through timing,
  counters, logs, and absence of backend errors.
- Phase 4: measure frame time, show time, missed budgets, and dither threshold
  errors under the minimal render loop.
- Phase 5: verify one locally issued playback command reaches the GPU command
  queue and expires correctly.
- Phase 6: verify the persistent FFT animation and center-to-edge wave together
  at 100 FPS with dithering still active. Physical LED strips are still optional
  for this phase.
- Phase 7: verify PubSub-fed animation, latest-state inputs, master-generated
  present strobe timing, repeated-frame behavior, and absence of previous-RMT
  backpressure at the target cadence.
- Later phases: run spoke, ring, strip, endpoint, and index diagnostics to
  verify physical sequence and orientation after LED strips are connected.

The first implementation milestone should aim for phases 1 through 5, then
continue into Phase 6 once the disconnected-device timing path is stable.
Efficiency tuning should happen during this bring-up rather than waiting for
the strips to be soldered.

## Open Design Points

- Confirm `animationCommandPayloadBytes`. It should fit the largest expected
  spawn config while staying comfortably below PubSub payload limits.
- Define the first generated animation config structs, likely
  `DiagnosticFillConfig`, `FftReactiveConfig`, and `CenterWaveConfig`.
- Finalize FFT, beat, and wheel wire payloads as they stabilize. The current
  checked-in draft shape is FFT as eight `uint16_t` band values, wheel as
  `position` plus `delta`, and beat as `group`, `bpm`, `energy`, and
  `tension`.
- Decide whether public `HsvColor`/`RgbColor` must remain completely FastLED-free
  in all headers. Current recommendation: keep project-local color structs at
  component boundaries and use FastLED `CRGB`/`CHSV` only inside the FastLED
  renderer/output implementation.
- Decide when to replace the local command/test hook with the real PubSub
  `Animation` topic. Current recommendation: use the local hook through Phase 6
  if PubSub tuning is still in progress.
- Choose the strobe GPIO and electrical fan-out details. The design assumes a
  master-driven present strobe, but the exact pin, pull configuration, and
  distribution wiring still need to be selected.
- Decide whether the production strobe should be exactly 100 Hz or slightly
  above 100 Hz if FastLED's temporal-dither threshold proves sensitive to
  measured cadence.

## Explicit Non-Goals For The First Port

- No Bluetooth, WiFi, WebServer, or websocket LED mirror on GPU nodes.
- No user-facing web interface on GPU nodes.
- No GPU-to-master LED frame readback in the first render pipeline.
- No full LED frames over PubSub or SPI.
- No heap allocation in animation submission, active animation ownership, layer
  storage, topology storage, or frame buffers.
- No broad refactor of PubSub, SPI, Clock, or platform base abstractions.
- No new dependencies beyond the existing GPU FastLED dependency.
- No generic runtime plugin system for arbitrary animation classes.
