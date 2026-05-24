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
- `LedDisplay/detail/Primitives.hpp` contains both reusable drawing helpers and
  whole effects that should be isolated animation classes.
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
frame-to-frame. This is likely related to the currently observed issue where
closely-following green and purple waves can produce white/pink flicker in some
pixels. Temporal dithering may make the artifact more visible, but the pipeline
should first remove ambiguous same-buffer overlap by using scratch, layers,
opacity, and stable blend rules.

Before considering this complete, run a visual validation case with two
immediately-following waves in different hues. Expected behavior: no isolated
white/pink sparkle, no frame-to-frame hue ping-pong, and no headache-inducing
per-pixel flicker.

## Animation File Layout

Animations should become isolated classes again. Do not keep adding behavior to
`Types.hpp`, `Display.hpp`, or `Primitives.hpp`.

Use one file per animation:

```text
include/LedDisplay/Animations/
  CenterWave.hpp
  DiagnosticFill.hpp
  FftBands.hpp
  PrimitiveDemo.hpp
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

A central registry file may include all animation files and define the bounded
variant:

```cpp
using AnimationPayload = std::variant<
    Animations::DiagnosticFill,
    Animations::CenterWave,
    Animations::PrimitiveDemo,
    Animations::WheelIndicator,
    Animations::FftBands>;
```

The registry should be boring glue only. It may map `AnimationKind` to
animation construction/update functions, but animation-specific config,
defaults, command helpers, and rendering stay in the animation file.

`PrimitiveDemo` can remain a temporary wrapper while bring-up effects are still
useful. Effects that become real animations should move out of primitive helper
code and into their own animation files.

## Display Component Split

Split the current `Display` responsibilities into smaller owned pieces:

- `Display`: lifecycle, task registration, and high-level ownership
- `Pipeline`: frame step orchestration
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

## Present Timing

The current strobe-driven render/show task is transitional. The intended model
is still:

- render task on core 1 builds complete frames ahead of presentation
- strobe ISR signals a small present path
- present path selects the newest complete frame and calls output
- if render is late, repeat the previous complete frame and count it
- do not present partially composed frames

This probably requires double buffering between render and present. Keep the
current single-task path only until layer composition is stable enough to
measure the real render budget.

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
   current monolithic `Types.hpp`.

2. Create the animation registry and move existing animations.
   Move `DiagnosticFill`, `CenterWave`, `PrimitiveDemo`, and the temporary FFT
   behavior into `Animations/` files. Keep output visually equivalent during
   this step.

3. Introduce `LayerStack`, scratch, and compositor.
   Start with one layer if needed, then add the full fixed layer set. The
   render path should become scratch -> layer -> final frame.

4. Implement stable blend semantics.
   Add `Alpha`, opacity, deterministic tie handling, and validation cases for
   overlapping waves.

5. Replace direct FFT handling with an updatable animation.
   Remove `Audio::FftFrame` knowledge from `Display`. Add an input/update path
   and a first `FftBands` or equivalent placeholder animation.

6. Add `WheelIndicator`.
   Start it persistently on the `Wheel` layer and update it from wheel state.

7. Refactor configuration.
   Move runtime tuning into `LedDisplay::Config` and split the remaining static
   bounds into focused static config types.

8. Split render and present.
   Add complete-frame buffering and repeated-frame accounting if measurements
   show the single task is no longer sufficient.

9. Add diagnostics and boot-scene playback.
   Verify topology, ownership, layer order, and cross-node synchronization.

10. Consolidate documentation.
    Merge this temporary plan, the legacy docs, and the original port plan into
    one current animation pipeline document. Archive or delete obsolete legacy
    notes once they are no longer needed.

## Definition Of Done

The refactor is done when:

- real animations can target independent layers without corrupting each other
- overlapping waves no longer create unstable white/pink flicker
- wheel and FFT are represented as long-running updatable animations
- the display core has no animation-specific FFT or wheel logic
- each animation owns its config, command helper, and render implementation
- `StaticConfig/LedDisplay.hpp` contains only true static bounds/selection
- the active environments build: `master`, `media`, `gpu0`, `gpu1`, and `io`
- hardware validation confirms strobe cadence, no missed-present storm, and
  usable visual composition at the configured brightness/dither settings

