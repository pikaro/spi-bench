# Temporary Animation Pipeline Refactor Plan

This is a temporary working plan for turning the current GPU LED animation
bring-up code into the actual animation pipeline. It should be consolidated
into the main animation documentation after implementation. Until then, treat
this document as the current direction for the refactor and feature-completion
work.

The previous implementation under `../led/upper/include` remains the behavioral
reference for the layer stack, animation isolation, composition model, and
updatable wheel-style animations. The old code should not be copied literally:
it used heap allocation, runtime polymorphism, dynamic containers, string
lookups, and a display/driver queue split that do not fit this repository. Its
pipeline shape is still the right reference.

## Compatibility Policy

There is no backwards-compatibility requirement for the animation pipeline
refactor. Do not preserve legacy include paths, temporary bring-up APIs, command
names, type aliases, file layouts, or behavior merely to avoid breaking current
callers. The only objective is a clean, maintainable final architecture for the
animation pipeline.

Compatibility shims are explicitly out of scope unless they are needed for a
short-lived local migration step and are removed before the implementation is
considered complete.

## Current Assessment

The checked-in GPU animation code is a useful hardware bring-up scaffold, not a
usable production animation pipeline. It can render local effects, receive
PubSub animation commands, consume FFT frames, drive compact FastLED buffers,
and synchronize presentation from the master GPIO strobe. That is enough to
prove wiring and cadence, but it is not enough for real animation development.

The critical missing pieces are:

- no fixed layer stack
- no per-layer buffers
- no animation scratch buffer
- no ordered composition pass
- no per-layer or per-animation opacity
- no alpha blend operation
- no layer decay policy
- no render-ahead/present split
- no long-running animation update path
- no wheel indicator animation
- no clean animation class boundaries
- no clean configuration boundary

The current implementation also has structural problems that must not become
patterns for later work:

- `LedDisplay/Interfaces/Types.hpp` is a mixed dump of colors, commands,
  animation configs, runtime animation variants, layers, primitive IDs, and
  wire payloads.
- `LedDisplay/detail/Display.hpp` owns lifecycle, commands, PubSub
  subscriptions, command decoding, animation storage, FFT state, strobe input,
  rendering, timing diagnostics, and output calls in one large class.
- `Display` currently exposes animation-specific public methods such as local
  wave and primitive-demo helpers, and it contains render overloads for
  concrete animations. This is the wrong dependency direction. The display
  surface must be generic and must not know how to build, decode, or render
  individual animations.
- `LedDisplay/detail/Primitives.hpp` contains both reusable drawing helpers and
  whole effects. That makes the primitives unusable as primitives. A primitive
  should be a small reusable drawing/effect kernel that an animation can call;
  it should not own lifetime, PubSub commands, layers, or animation state.
- The display core includes `Audio/Interfaces/Wire.hpp` directly and therefore
  knows about FFT. FFT should be an update input consumed by an animation, not a
  special case in the display core.
- `AnimationCommandFactory.hpp` centralizes command construction for all
  animations. Command helpers should live with the animation that owns the
  config and semantics.
- `StaticConfig/LedDisplay.hpp` mixes topology constants, ownership selection,
  output pins, runtime tuning, task settings, buffer bounds, and animation
  command sizes in one monolithic type.
- The public facade currently reaches through to a detail-heavy implementation
  header, causing downstream includes to inherit more implementation detail than
  needed.

## Target Shape

The target pipeline should restore the useful old concepts in the current
repository style:

1. Outside code posts animation commands or state updates.
2. The display/render owner drains those inputs at frame boundaries.
3. Each active animation renders into one scratch buffer.
4. Scratch is blended into that animation's target layer.
5. Fixed layers are composed in priority order into the final frame.
6. The output backend converts and presents the final local frame.

All buffers are fixed storage sized for this GPU node's owned pixels. Hot loops
iterate local pixels only. Animation ownership remains fixed-slot and
allocation-free. PubSub still carries compact commands and state, never full LED
frames.

## Responsibility Map

The split of responsibility is the central design constraint. The code should
make illegal ownership hard to introduce.

### `Display`

`Display` owns the device-facing lifecycle and presentation boundary:

- component lifecycle and task registration
- present-strobe GPIO setup, ISR accounting, and task wakeup
- PubSub subscription and unsubscription
- generic command/update ingress
- final output backend initialization and `show()`
- timing diagnostics around strobe cadence and presentation

`Display` may accept an opaque `AnimationCommand` from local commands or PubSub
and forward it to the engine. It must not expose helpers such as
`playCenterWave()`, `playDefaultWave()`, `playPrimitive()`, or any future
animation-specific method. Local commands and PubSub commands must travel the
same generic command path.

`Display` must not:

- switch on concrete animation kinds
- decode animation config payloads
- contain render overloads for animations
- own FFT or wheel rendering behavior
- build animation configs
- choose animation layers for concrete animations

### `AnimationEngine`

`AnimationEngine` is the small backend behind `Display`. It owns animation
requests and frame construction:

- fixed active animation slots
- request ID and generation handling
- start/stop/update command handling
- decode-and-construct dispatch through the animation registry
- latest-value input state for update streams such as FFT and wheel
- frame-clock creation for animations
- active animation expiration
- per-frame sequence: update state, clear/decay layers, render animations into
  scratch, blend scratch into layers, compose layers into the final frame

The engine may switch on `AnimationKind`, but only to dispatch into the
registry or a concrete animation's own construction/update helpers. It should
remain light: no heap allocation, no dynamic polymorphism, no string lookup,
and no command-specific one-off rendering paths.

### `LayerStack` And `Compositor`

`LayerStack` owns layer buffers and scratch storage. It is the only place that
knows which layer buffers exist and how they decay or clear.

`Compositor` owns blend semantics:

- animation scratch -> target layer, using animation opacity/blend settings
- ordered layer buffers -> final frame, using layer opacity/blend settings
- deterministic hue ownership for ties and near-ties

Neither class should know about concrete animation kinds.

### `Animations::*`

Each concrete animation owns its own semantics:

- commandable config struct
- optional update payload struct
- default layer, lifetime, blend, and opacity
- command helper or command descriptor
- construction from decoded config
- update handling for typed update payloads
- render implementation into the scratch/canvas supplied by the engine

Animations may call primitives. They must not reach into `Display` or the
output backend.

### `Primitives::*`

Primitives are reusable drawing kernels and math helpers only. They have no
identity in the active animation set and no command surface.
They render into the animation scratch buffer supplied by the engine, normally
through a lightweight canvas/writer reference. A primitive must never allocate
or retain a frame buffer, and it must never iterate the whole display unless
its documented effect is inherently a whole-display primitive.

Fixed storage sized from enums should use `magic_enum::enum_count<T>()` where
appropriate. Do not add protocol-visible sentinel members such as `Count` only
to size arrays.

Configuration defaults should remain the single source of truth on the config
fields and animation default members that own them. Do not introduce parallel
`Defaults` namespaces merely to give obvious default literals names. Named
constants are required for non-obvious values used inside calculations,
hashing, fixed-point scaling, timing conversion, decay policies, or other
algorithmic code.

## Layer Stack

Add a fixed layer stack based on the old working model:

- `Background`
- `Fft`
- `Effect`
- `Wheel`
- `Debug`

Each layer should have:

- fixed local HSV buffer
- enabled flag
- blend operation
- opacity
- clear or decay policy
- decay amount, if decay is enabled
- fixed priority/order known at compile time

The default policies should start close to the legacy intent:

- `Background`: persistent, `MaxValue`, high decay or redraw-each-frame
- `Fft`: `MaxValue`, moderate decay
- `Effect`: `MaxValue`, light decay
- `Wheel`: alpha-capable overlay, light decay or redraw-each-frame
- `Debug`: `AddValue` or explicit diagnostic blend, stronger decay

The first implementation can tune the exact values later. The important part is
the separation: background, FFT, wheel, one-shot effects, and diagnostics must
not fight in one shared frame buffer.

## Composition And Blending

Reintroduce the two-stage blend model:

1. animation scratch into target layer, using animation blend settings
2. ordered layer buffers into final frame, using layer blend settings

Supported operations should include at least:

- `Replace`
- `MaxValue`
- `AddValue`
- `Alpha`

`MaxValue` and `AddValue` need deterministic tie behavior. Equal or near-equal
values from overlapping effects must not cause hue ownership to flip
frame-to-frame. Known visual/rendering bugs are tracked separately in
[animation-pipeline-known-bugs.md](animation-pipeline-known-bugs.md).

## Animation File Layout

Animations should become isolated classes again. Do not keep adding behavior to
`Types.hpp`, `Display.hpp`, or `Primitives.hpp`.

Use one file per animation:

```text
include/LedDisplay/Animations/
  CenterWave.hpp
  DiagnosticFill.hpp
  FftBands.hpp
  WheelIndicator.hpp
```

Each animation file owns:

- its `WIRE_MSG` config struct, if it is commandable over PubSub
- its runtime animation class
- its default layer
- its default lifetime policy
- its default blend/opacity settings
- its command helper or command descriptor
- its update payload type, if it is updatable
- its render implementation

The command declaration belongs with the animation. Codec-dependent inline
definitions may live in a small `Animations/Commands.hpp` aggregation header so
generated wire metadata can include animation config headers without recursively
including `PubSubBackend/detail/Codec.hpp`.

A central registry file may include all animation files and define the bounded
variant:

```cpp
using AnimationPayload = std::variant<
    Animations::DiagnosticFill,
    Animations::CenterWave,
    Animations::WheelIndicator,
    Animations::FftBands>;
```

The registry should be boring glue only. It may map `AnimationKind` to
animation construction/update functions, but animation-specific config,
defaults, command declarations, and rendering stay in the animation file.

`PrimitiveDemo` should be removed. Bring-up effects that remain useful should
either become real diagnostic animations with their own files or be reduced to
primitive kernels that real animations call. Do not preserve a commandable
primitive-demo wrapper.

## Primitive File Layout

Primitives are separate from animations. They are reusable drawing/effect
building blocks that animations call while rendering.

Use a separate directory:

```text
include/LedDisplay/Primitives/
  Falloff.hpp
  PixelSparkle.hpp
  Ring.hpp
  Spoke.hpp
  Trail.hpp
```

A primitive may:

- draw into a provided scratch/canvas/writer
- operate on logical coordinates, physical pixels, or local spans
- apply falloff, masks, gradients, or tiny repeated effects
- be deterministic from explicit parameters and frame time

A primitive must not:

- own an active animation slot
- subscribe to PubSub
- define an animation command
- define a lifetime policy
- choose a layer
- retain per-animation state unless that state is passed in by the caller

Example target shape: a `PixelSparkle` primitive adds a glimmer at `(spoke,
radial)` with surrounding radial/angular falloff. A `RandomSparkle` animation
can then choose 20 pixels at spawn time and invoke the primitive for each one.
That primitive writes only the center pixel and bounded falloff neighborhood
into the caller-provided scratch/canvas. It does not scan every pixel looking
for sparkle candidates.

The old bring-up primitive header contained animation-like demos such as
`Fire`, `Comet`, and `Rainbow`. During refactor, split this apart:

- reusable low-level operations become true primitives
- commandable/time-owning effects become animations
- temporary diagnostics remain explicitly named diagnostics

The initial implementation step removes the previous full-scene primitive
bundle entirely. Wave and FFT fallback rendering live in their animation
classes until there is a real reusable primitive worth extracting.

## Display Component Split

Split the current `Display` responsibilities into smaller owned pieces:

- `Display`: lifecycle, task registration, and high-level ownership
- `AnimationEngine`: generic command handling and frame step orchestration
- `AnimationSlots`: fixed active slot storage, generation/request ID handling,
  expiration
- `CommandQueue`: queue-copyable command ingress and decode boundary
- `InputRouter`: PubSub-fed state/update ingress for active animations
- `LayerStack`: layer buffers, decay/clear, and ordered composition
- `Compositor`: blend functions and opacity handling
- `RenderContext`: frame clock, topology access, scratch writer, state snapshot
- `Present/Strobe`: strobe accounting and eventual render/present split
- `Output`: FastLED-backed conversion and `show()`

This split should not introduce heap allocation or virtual dispatch. The goal
is ownership clarity, not an abstract plugin system.

The display boundary is intentionally narrow: it forwards generic requests to
the engine and presents the final frame. Any command path that says "wave",
"fill", "FFT", "wheel", or any future animation name must live outside
`Display` and eventually call the same generic engine ingress as PubSub.

## Updatable Animations

Long-running animations need a first-class update path. A running animation
should be started once, then updated by PubSub state without respawning or
reallocating it.

The command model should support:

- `Play`: create or replace a fixed animation slot
- `Stop`: stop by request ID or stop all
- `Update`: apply a bounded update payload to an active animation or typed
  state slot
- global state updates such as hue and rotation offsets

Updates should be delivered at frame boundaries and copied into fixed storage.
High-rate updates should use latest-value semantics rather than command queues
that grow under load.

### Wheel Indicator

The wheel indicator is required for the final setup. It should be a persistent
animation on the `Wheel` layer.

Behavioral reference from the old implementation:

- render a small group of spokes as a blue overlay/background
- wheel updates rotate which spokes are highlighted
- the highlighted spokes coexist with background and effects through layer
  composition and alpha blending

The new implementation should start the animation once and then update its
angle/offset from wheel PubSub state. The display core should not hard-code
wheel behavior; `WheelIndicator` owns how wheel state affects rendering.

### FFT

FFT should not be special-cased in the GPU display core. The current direct
`FftFrame` subscription inside `Display` should be removed during the refactor.

FFT is another updatable animation input. A possible first real animation is
`FftBands` on the `Fft` or `Background` layer:

- start `FftBands` once
- each FFT frame updates fixed latest-state storage
- render each strip or spoke with band width proportional to the latest band
  magnitude
- keep the mapping and visual policy inside `FftBands`, not in `Display`

The exact FFT visual is intentionally undecided. The important architectural
rule is that FFT frames update an animation; they are not a display-core
special case and they are not repeated spawn commands.

## Configuration Refactor

`StaticConfig/LedDisplay.hpp` should only contain values that truly must be
compile-time constants for storage sizing, topology selection, or build-role
selection.

Keep in static config:

- umbrella topology dimensions
- maximum layer count
- maximum active animation count
- command queue and payload byte bounds
- output data-line maximums
- total group count and node group ownership, where selected by firmware image
- renderer/output backend selection
- task stack size, if that follows the existing task-stack config pattern

Move to normal `LedDisplay::Config` passed from `src/gpu/config.hpp`:

- global brightness
- temporal dithering enablement
- frame target/fallback interval
- layer default policies
- animation default durations and values
- output pin list for the concrete board profile, unless template APIs force
  pin values to remain compile-time
- strobe fallback behavior
- logging/profiling switches that do not affect storage layout

Break the remaining static config into smaller named types instead of one
giant `LedDisplayConfig`. Candidate split:

- `LedTopologyStaticConfig`
- `LedOwnershipStaticConfig`
- `LedOutputStaticConfig`
- `LedPipelineBounds`
- `LedAnimationBounds`

Build flags should remain narrow. Node identity and per-firmware ownership are
valid build differentiation. Ordinary tuning should be constexpr config in code
or a normal config object, not new `-D...` flags.

## Command And Wire Model

Keep one fixed-size PubSub animation command topic for low-rate commands. Each
animation owns its config and command helper, but the generic publish path can
remain shared.

Recommended split:

- `Interfaces/AnimationCommand.hpp`: generic wire envelope shape
- `Interfaces/AnimationKind.hpp`: stable wire enum
- `Animations/<Name>.hpp`: config, update payload, command helper, runtime
  class
- `Animations/Registry.hpp`: variant and kind-to-animation dispatch
- `Support/CoreCommands.hpp`: command-line aggregation only; no animation
  semantics beyond forwarding to the owning animation helper

Generated wire support should discover animation config structs through an
`Animations/All.hpp` include rather than through one giant type header.

Local GPU commands are allowed for diagnostics, but they must not be private
display shortcuts. They should construct the same `AnimationCommand` that would
arrive over PubSub and submit it through the same generic request path.

## Present Timing

The current strobe-driven render/show task is transitional. The intended model
is still:

- render task on core 1 builds complete frames ahead of presentation
- strobe ISR signals a small present path
- present path selects the newest complete frame and calls output
- if render is late, repeat the previous complete frame and count it
- do not present partially composed frames

Buffering must be selectable. The old implementation exposed
`LedDriverBufferMode::{Unbuffered, DoubleBuffer, TripleBuffer}` and selected
triple buffering in `../led/upper/src/upper/main.cpp`. The old
`FrameInbox` classes used a `put()`, `trySwap()`, `current()`, and
`haveConsumed()` contract; triple buffering copied into a staging buffer before
the short critical-section swap. The new implementation should preserve the
useful mode selection while replacing the old heap-backed vectors and virtual
inbox with fixed storage and static dispatch.

Required buffering modes:

- `None`: render and present use the same frame owner. Useful only for early
  diagnostics and minimal memory validation.
- `Double`: render writes one complete frame while present owns another.
- `Triple`: render, ready, and presenting frames are distinct, reducing
  producer/consumer phase contention.

The design should leave room for `Quadruple` buffering. At 125 FPS and with the
current FFT cadence around 31 Hz, memory and timing budget should be sufficient
for deeper buffering if measurement shows it helps. Do not make quadruple
buffering the initial requirement, but avoid baking in a two-or-three-only
assumption.

## Diagnostics And Visual Bring-Up

Add diagnostics after the layer pipeline exists, not before:

- ring diagnostic
- spoke diagnostic
- strip/data-line diagnostic
- endpoint diagnostic
- physical index walk
- layer isolation diagnostics
- blend diagnostics with known overlapping colors

These diagnostics should verify both topology and composition. They replace the
old stale diagnostics that assumed small LED counts.

## Migration Sequence

1. Split type/config headers without changing behavior.
   Move color, blend, layer, command, and animation kind types out of the
   current monolithic `Types.hpp`. Delete the aggregate header instead of
   preserving it as a compatibility include.

2. Create the animation registry and move existing animations.
   Move `DiagnosticFill`, `CenterWave`, and the temporary FFT behavior into
   `Animations/` files. Remove `PrimitiveDemo` instead of migrating it. Keep
   remaining output visually equivalent during this step.

3. Create the primitive directory and split current primitive-like code.
   Move reusable drawing kernels into `Primitives/`. Keep commandable effects
   in `Animations/` or diagnostic animation files. Delete the old
   `detail/Primitives.hpp` path rather than keeping a shim.

4. Introduce `LayerStack`, scratch, and compositor.
   Start with one layer if needed, then add the full fixed layer set. The
   render path should become scratch -> layer -> final frame.

5. Move animation ownership out of `Display`.
   Introduce `AnimationEngine` as the backend for generic play/stop/update
   requests. Delete display methods and render overloads that know about
   concrete animations. Local commands and PubSub must use the same generic
   request path.

6. Implement stable blend semantics.
   Add `Alpha`, opacity, deterministic tie handling, and frame-level validation
   cases for overlapping effects.

7. Replace direct FFT handling with an updatable animation.
   Remove `Audio::FftFrame` knowledge from `Display`. Add an input/update path
   and a first `FftBands` or equivalent placeholder animation.

8. Add `WheelIndicator`.
   Start it persistently on the `Wheel` layer and update it from wheel state.

9. Refactor configuration.
   Move runtime tuning into `LedDisplay::Config` and split the remaining static
   bounds into focused static config types.

10. Split render and present.
   Add selectable `None`, `Double`, and `Triple` buffering modes, complete-frame
   handoff, and repeated-frame accounting. Keep room for a `Quadruple` mode.

11. Add diagnostics and boot-scene playback.
   Verify topology, ownership, layer order, and cross-node synchronization.

12. Consolidate documentation.
    Merge this temporary plan, the legacy docs, and the original port plan into
    one current animation pipeline document. Archive or delete obsolete legacy
    notes once they are no longer needed.

## Definition Of Done

The refactor is done when:

- real animations can target independent layers without corrupting each other
- wheel and FFT are represented as long-running updatable animations
- the display core has no animation-specific FFT or wheel logic
- the display public API has no animation-specific helpers
- local commands and PubSub requests share the same generic engine path
- each animation owns its config, command helper, and render implementation
- primitives are reusable kernels in `Primitives/`, not disguised animations
- `PrimitiveDemo` and its command surface are gone
- `StaticConfig/LedDisplay.hpp` contains only true static bounds/selection
- render/present buffering supports at least none, double, and triple modes
- known visual bugs have offline-render reproductions or documented hardware
  validation steps
- the active environments build: `master`, `media`, `gpu0`, `gpu1`, and `io`
- hardware validation confirms strobe cadence, no missed-present storm, and
  usable visual composition at the configured brightness/dither settings
