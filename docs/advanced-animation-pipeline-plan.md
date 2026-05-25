# Advanced animation pipeline implementation plan

This document turns [anim-advanced-analysis.md](anim-advanced-analysis.md) into
a concrete implementation sequence. The goal is a quick, low-risk launch slice
that tests whether radial field animation is valuable on the current 16 by 46
umbrella surface.

The plan deliberately favors low-complexity, low-CPU, high-impact work. It does
not attempt reaction diffusion, Turing fields, Lenia, heavy fBM, runtime
formula engines, or projectM/MilkDrop compatibility.

## Assumptions

- The current animation pipeline shape is acceptable. Changes should be
  extensions/toolkits unless a small transparent replacement is clearly cleaner.
- Backwards compatibility is not required. Existing animation configs and
  command names may change if that keeps the implementation simpler.
- The first launch should prove visual value and radial resolution, not
  exhaust the algorithm space.
- Offline rendering must be updated together with firmware-facing changes.
- Full-frame rendering is an inherent per-animation property. It should be a
  `static constexpr` flag on animation classes and should only be enabled for
  animations that need full logical-frame memory.

## First launch scope

Ship these capabilities first:

- compact logical coordinate helpers for polar field rendering
- GPU-side FFT/audio conditioning with smoothed bass/mid/high/energy values
- small field math toolkit: palettes, waves, folds, ring pulses, simple masks
- a replacement `FftReactive` visual based on polar waves and masks
- one simple background field animation, if the toolkit makes it nearly free
- one effect-layer particle/spark primitive, if it stays bounded and cheap
- offline renderer support for the new context and examples

Do not ship in the first launch:

- feedback warp
- reaction-diffusion or Turing simulations
- boids
- full noise/fBM field engine
- new runtime preset language
- cross-GPU state exchange

## Protected invariants

- GPU output still presents only physically owned LEDs.
- FastLED remains an output backend concern, not a dependency of animation
  primitives.
- Animation rendering remains frame-bound and deterministic.
- Large state does not go into ordinary animation payload variants.
- Hot render paths avoid heap allocation.
- Existing layer ownership remains meaningful: background/FFT/effect/wheel
  should not collapse into one shared frame buffer.
- Any animation that renders full logical frames must still project only owned
  pixels to the physical output.

## Phase 1: animation traits and render extent

Add a tiny trait surface to every animation:

```cpp
static constexpr bool requiresFullFrame = false;
```

For existing animations this is `false`.

Meaning:

- `false`: render exactly as today into owned-pixel scratch through the current
  logical-to-local map.
- `true`: render a complete logical 16 by 46 frame locally on each GPU, then
  project the owned pixels into the normal layer stack for composition/output.
  This costs CPU, but it preserves the current advantage that each GPU still
  presents through its own output/RMT resources. It also keeps the path open
  for using more ESP32-S3 RMT channels per GPU later without changing the
  animation's logical model.

Implementation steps:

1. Add `requiresFullFrame` to all current animation structs.
2. Add `Animations::requiresFullFrame(payload)` in
   `include/LedDisplay/Animations/Registry.hpp`.
3. Update `tools/led-render/generate_registry.py` to emit
   `Generated::requiresFullFrame(payload)`.
4. Add trace metadata for every animation:
   `requires_full_frame: true/false`.
5. Keep all current animations on the owned-frame path.

Why this belongs first:

- It makes the future feedback path explicit before any animation depends on
  it.
- It avoids hidden special cases in `AnimationEngine`.
- It lets host-render traces show whether an animation is expected to use the
  full logical surface.

Firmware render path:

- Keep `LayerStack` owned-pixel based.
- Add one reusable full logical scratch frame in `AnimationEngine`, sized
  `Config::totalPixelCount`.
- Add an identity logical map for full-frame scratch rendering.
- For `requiresFullFrame == true`, clear full scratch, render into it, then
  project full logical pixels through the existing rotated ownership map into
  the normal owned scratch/layer path.
- Do not add full-frame layer buffers yet. Feedback can later add its own
  explicit previous-frame state.

This costs about 2.2 KB for one HSV full-frame scratch on the current topology.

## Phase 2: coordinate toolkit

Add a reusable coordinate helper that is independent of concrete animations.

Suggested location:

- `include/LedDisplay/Primitives/FieldCoordinates.hpp`

Core types:

```cpp
struct FieldPoint {
    uint8_t spoke;
    uint8_t radial;
    uint8_t theta;     // 0..255 turn
    uint16_t stripRadius; // 0..65535 along the visible strip
    uint16_t worldRadius; // 0..65535 from geometric center to outer edge
    int16_t x;         // Q1.14 or Q1.15 style normalized coordinate
    int16_t y;
};
```

Implementation steps:

1. Add a 16-entry sine/cosine table or generate one at compile time if the
   current toolchain allows clean constexpr math.
2. Add geometry constants for the physical annulus: an inner empty radius and
   a visible strip length. The current hardware is roughly 30 cm inner gap and
   30 cm strip length, so LED centers occupy about the outer half of the
   radius.
3. Add a 46-entry strip-radius table using radial cell centers.
4. Derive `worldRadius` from `(innerRadius + stripRadius) / outerRadius` before
   computing `x` and `y`.
5. Expose `fieldPoint(spoke, radial)`.
6. Expose `forEachLogicalPixel(callback)` or a small iterator helper.
7. Keep all public values fixed-point or integer. Use `float` only in tests or
   host-only helper code if needed.
8. Add a short doc comment that these coordinates describe logical animation
   space, not physical wiring.

Design notes:

- `theta` should be turn-based, not radians.
- `stripRadius` should use `(radial + 0.5) / ringCount`.
- `worldRadius` must account for the center gap. Do not treat radial index zero
  as the center of the sculpture.
- Angular resolution is only 16 samples; helpers should make low angular modes
  easy and high modes visibly deliberate.
- This helper should be useful for later feedback, Voronoi, SDF masks, and
  particles, not only the first FFT visual.

## Phase 3: canvas and projection helpers

Extend the drawing toolkit enough that animations can target either owned or
full logical scratch without duplicating loops.

Implementation steps:

1. Add an identity logical map helper for full logical frames.
2. Add a projection helper:
   `projectLogicalFrameToOwnedScratch(fullFrame, ownedScratch, map, style/op)`.
3. Add optional `Canvas::sample(spoke, radial)` or a small standalone sampler.
4. Add `Canvas::pixel(FieldPoint, color)` convenience only if it removes
   repeated boilerplate.
5. Keep `Canvas` simple. Do not turn it into a scene graph or state owner.

The projection helper is the key piece for `requiresFullFrame`. It keeps the
normal layer stack local while allowing selected animations to compute complete
logical fields.

## Phase 4: audio conditioning

Add a small GPU-side audio state derived from the latest FFT frame.

Suggested location:

- `include/LedDisplay/Primitives/AudioControls.hpp`, or
- `include/LedDisplay/Interfaces/AudioControls.hpp` if it becomes part of
  `AnimationRenderContext`.

Suggested state:

```cpp
struct AudioControls {
    uint8_t bass;
    uint8_t mid;
    uint8_t high;
    uint8_t energy;
    uint8_t bassAttack;
    uint8_t midAttack;
    uint8_t highAttack;
    bool hasInput;
};
```

Implementation steps:

1. Derive raw bass from `subBass` and `bass`.
2. Derive raw mid from `lowMid`, `mid`, and `highMid`.
3. Derive raw high from `presence`, `brilliance`, and `air`.
4. Derive `energy` as a cheap average or max blend of all three.
5. Smooth with integer attack/release:
   fast rise, slower fall.
6. Derive attack values from positive deltas before smoothing or from the
   smoothed/raw gap.
7. Store this smoothed state in `AnimationEngine`, updated once per rendered
   frame.
8. Add it to `AnimationRenderContext`.

Avoid subscribing GPUs to beat events in this first launch. Local onset values
from FFT deltas are enough to test whether field visuals react well.

Host-render steps:

1. Extend JSON `inputs.fft` as needed only if field tests require it.
2. Add optional synthetic audio timelines:

```json
{
  "audio": {
    "mode": "sine",
    "bass": {"base": 40, "amp": 180, "period_ms": 900},
    "mid": {"base": 60, "amp": 100, "period_ms": 1400},
    "high": {"base": 20, "amp": 120, "period_ms": 180}
  }
}
```

3. Keep static `inputs.fft` support. Existing examples should remain easy to
   read even if backwards compatibility is not a formal requirement.

Synthetic audio is useful because a static FFT snapshot does not exercise
envelope motion.

## Phase 5: field math primitives

Add a compact toolkit for low-cost field effects.

Suggested location:

- `include/LedDisplay/Primitives/FieldMath.hpp`

First-launch helpers:

- `scale8`, `qadd8` reuse through renderer utilities where possible
- `absDiff8`
- `triangle8`
- `smoothstep8`
- `pulse8(center, width, position)`
- `ringPulse(radius, center, width)`
- `angularDistance(thetaA, thetaB)`
- `foldAngle(theta, segments)`
- `standingWave(thetaMode, radialMode, point, phase)`
- `palette2` or `palette3` between HSV colors
- `applyEnergy(value, audio.energy, sensitivity)`

Do not add:

- general fBM
- general noise octaves
- dynamic allocation
- float-heavy shader helpers

If a simple hash/value-noise helper is added, keep it as a separate final
commit in this phase so it can be dropped if timing is bad.

## Phase 6: first advanced FFT visual

Replace the current placeholder `FftReactive` with a polar field visual.

This can reuse the existing animation kind and file if that is simpler.
Backwards compatibility is not required.

Suggested config:

```cpp
struct WIRE_MSG FftReactiveConfig {
    uint8_t baseHue = 144;
    uint8_t saturation = 255;
    uint8_t value = 180;
    uint8_t radialMode = 2;
    uint8_t angularMode = 4;
    uint8_t symmetry = 4;
    uint8_t contrast = 128;
    uint8_t beatSensitivity = 128;
};
```

Visual model:

- Use radial standing waves driven by bass.
- Use folded angle/symmetry driven by mid.
- Use high/attack to sharpen bright ridges or add small edge highlights.
- Use `energy` to scale brightness/contrast.
- Avoid per-pixel trig. Use integer phases and triangle/sine-like lookup
  helpers.

Implementation steps:

1. Render every logical pixel through `FieldPoint`.
2. Compute a radial wave component.
3. Compute an angular folded component.
4. Combine them with audio controls into a value.
5. Map value to HSV.
6. Render on the `Fft` layer with `requiresFullFrame = false`.
7. Keep the animation persistent by default.

Acceptance criteria:

- It looks meaningfully different from radial bars in the offline radial
  viewer.
- It still reads as coherent at 16 spokes.
- It avoids full-frame rendering.
- Hardware metrics keep `ledDisp.slow == 0` and `stepMax` comfortably below
  8000 us.

## Phase 7: optional simple background field

Add a low-cost background only if phases 2-6 leave the code clean.

Suggested animation:

- `FieldBackground`

Visual model:

- slow radial/angle color drift
- folded angular symmetry
- low value, low contrast
- no FFT required, but optionally reacts to `energy`

Layer:

- `Background`

Render extent:

- `requiresFullFrame = false`

This tests whether the toolkit can support non-FFT fields without more engine
work. If adding a new animation kind and commands starts to expand scope, skip
this phase for the first launch.

## Phase 8: optional effect-layer particle/spark primitive

Add this only if the first FFT field proves the coordinate and audio helpers
are stable.

Suggested animation:

- `SparkBurst` or `AccentParticles`

Visual model:

- 8-32 particles
- fixed-point position in logical polar coordinates
- simple radial/azimuth velocity
- value decay handled by the `Effect` layer where possible
- optional shape choices: dot, two-pixel star, short radial streak

Layer:

- `Effect`

Render extent:

- `requiresFullFrame = false`

Implementation notes:

- Keep state bounded and explicit.
- Do not make this a full boids system.
- It can share splat primitives with future flow-field particles and sprite
  accents.

## Phase 9: command and orchestration updates

Keep control simple.

Implementation steps:

1. Update `/anim` helpers for the new `FftReactive` config.
2. Add one command preset for the advanced FFT field.
3. Start the persistent FFT field from master after the bring-up sweep window,
   if that matches the current show flow.
4. Keep beat-triggered `CenterWave` or replace it only if it visually fights
   the new FFT field.
5. Do not add a generalized preset system in this launch.

Because backwards compatibility is not required, it is fine to change the
manual command arguments for `FftReactive` if the new config needs different
controls.

## Phase 10: offline render tooling

Tooling changes are part of the launch, not a follow-up.

Implementation steps:

1. Update `tools/led-render/generate_registry.py` for
   `requiresFullFrame`.
2. Update host render metadata to include each animation's render extent.
3. Update `HostRuntime.hpp` and `host_render.cpp` for `AudioControls`.
4. Add synthetic audio timeline support or a simpler repeating input generator.
5. Add examples:
   - `fft-polar-static.json`
   - `fft-polar-audio-sweep.json`
   - optional `field-background.json`
6. Keep trace pixel order logical `(spoke, radial)`.
7. If full-frame scratch support is added in firmware, mirror it in the host
   renderer even if no first-launch animation uses it.

Optional analysis addition:

- Add a small angular aliasing report that compares adjacent spoke deltas.
  This can help decide whether 16 spokes are too coarse for a given mode.

Do not block the first firmware launch on new analysis reports. The radial
viewer and still captures are enough for the first decision.

## Phase 11: validation sequence

Offline validation:

```sh
bin/led-render \
  --config tools/led-render/examples/fft-polar-audio-sweep.json \
  --output /tmp/fft-polar.tled
bin/led-analyze summary /tmp/fft-polar.tled --stats
bin/led-analyze flicker /tmp/fft-polar.tled --hue-ping-pong
bin/led-view /tmp/fft-polar.tled --layout radial --glare
```

Build validation:

```sh
bin/build -e master
bin/build -e media
bin/build -e gpu0
bin/build -e gpu1
```

Build `io` too if shared PubSub/wire files or orchestration paths are touched:

```sh
bin/build -e io
```

Hardware validation:

```text
!gpu0 /metrics
!gpu1 /metrics
!master /anim <new-fft-field-command>
!gpu0 /metrics
!gpu1 /metrics
```

Acceptance criteria:

- no command decode failures
- no render failures
- no missed strobes
- `ledDisp.slow == 0`
- `stepMax < 8000 us`, preferably with at least 2 ms headroom
- visual field is readable in radial layout and on hardware
- no obvious spoke aliasing for default parameters

## Phase 12: first follow-up if launch looks promising

If the first field visuals are useful, the next step is a constrained feedback
prototype.

Feedback-specific additions:

- Set `requiresFullFrame = true`.
- Add animation-owned previous full logical frame state.
- Use nearest-neighbor sampling first.
- Keep warp offsets small.
- Decay and inject in HSV value space initially.
- Project owned pixels into the normal layer stack after updating full state.

Do not add cross-GPU transport for previous frames. The whole point of
`requiresFullFrame` is that each GPU deterministically computes the same full
logical field locally, then outputs only its own pixels.

## File checklist

Likely firmware files:

- `include/LedDisplay/Animations/FftReactive.hpp`
- `include/LedDisplay/Animations/FftReactiveCommands.hpp`
- `include/LedDisplay/Animations/Registry.hpp`
- `include/LedDisplay/Interfaces/RenderContext.hpp`
- `include/LedDisplay/Primitives/Canvas.hpp`
- `include/LedDisplay/Primitives/FieldCoordinates.hpp`
- `include/LedDisplay/Primitives/FieldMath.hpp`
- `include/LedDisplay/Primitives/AudioControls.hpp`
- `include/LedDisplay/detail/AnimationEngine.hpp`
- `include/LedDisplay/detail/LayerStack.hpp` only if projection helpers belong
  there
- `src/master/orchestration.hpp` only if startup policy changes
- command helper files only for changed `/anim` manual controls

Likely tooling files:

- `tools/led-render/generate_registry.py`
- `tools/led-render/HostRuntime.hpp`
- `tools/led-render/host_render.cpp`
- `tools/led-render/README.md`
- `tools/led-render/examples/*.json`
- `tools/led_render_py/led_render/trace.py` only if trace planes/format change
- `tools/led_render_py/led_render/analysis.py` only for optional aliasing
  analysis

Likely docs:

- `docs/animation-pipeline.md`
- this plan, updated with final decisions if implementation changes direction

## Decision gates

Gate 1: after coordinate/audio/toolkit work.

- Host renderer builds.
- Existing animations still render.
- No visible behavior change required yet.

Gate 2: after `FftReactive` replacement.

- Offline radial trace looks coherent.
- GPU builds pass.
- Manual command can launch the effect.

Gate 3: after hardware validation.

- Metrics show frame budget is still healthy.
- Visual result is good enough to justify feedback/particles.

If Gate 2 fails because the radial resolution is too low, stop before adding
more animation families. Keep the coordinate/audio helpers only if they make
existing animations cleaner.

## Summary

The quickest useful path is not a new animation engine. It is a compact field
toolkit plus one serious FFT visual that can be rendered offline and on GPU
within the existing layer model. The only structural addition worth making
early is the per-animation `requiresFullFrame` flag, because it keeps later
feedback work explicit without forcing all animations through the heavier path.
