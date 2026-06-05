# Simple Fire-And-Forget Animation Plan

## Purpose

Add a set of simple, highly visible procedural animations for the radial LED
surface. These are meant to sit beside the current `CenterWave`, `Sinelon`, and
`SineWave` effects: easy to trigger, easy to preview locally, and useful as
standalone show accents.

These animations are not sprites. They should not try to render low-resolution
icons such as hearts, faces, or bitmap-like pictures. They should be calculated
from spoke, ring, time, and a small parameter payload.

The planned animation names are:

- `Starburst`
- `Vortex`
- `Shutter`
- `OrbitRing`
- `Lighthouse`
- `Cymatic`
- `BreathingRings`
- `RadialCurtain`
- `PolarLattice`
- `Bolt`

`Shutter` is the aperture-style animation name. Do not use `Iris` for this
simple animation set; that name is reserved elsewhere.

## Baseline Constraints

- The active LED surface is 16 spokes by 46 rings.
- The physical surface is an annulus, not a disk. Ring `0` is the inner visible
  ring, not the geometric center.
- GPU rendering runs at 125 Hz with an 8 ms present-and-render budget.
- Each GPU renders only its owned pixels through the logical-to-local map.
- Animation command payloads are fixed-size queue-copyable structs.
- `Animations::Payload` is a `std::variant` stored in every animation slot.
  Animation structs must not contain large state.
- Existing field helpers already cover most of the needed math:
  `Primitives::FieldCoordinates` exposes spoke, radial, theta, strip radius,
  physical radius, and fixed-point Cartesian coordinates.
- Existing field helpers already include low-cost shaping functions:
  `Primitives::FieldMath::scale8`, `qadd8`, `triangle8Q8`, `sine8Q8`,
  `smoothstep8`, `ringPulseQ8`, `foldedAngle`, and `palette2`.
- Host preview should use the production animation code through
  `tools/led-render/`, not a separate preview implementation.

## Scope

Implementation for each animation should touch only the normal animation
surface:

- `include/LedDisplay/Animations/<Name>/Config.hpp`
- `include/LedDisplay/Animations/<Name>/Animation.hpp`
- `include/LedDisplay/Animations/<Name>/Command.hpp`
- `include/LedDisplay/Animations/<Name>/CommandDesc.hpp`
- `include/LedDisplay/Interfaces/AnimationKind.hpp`
- `include/LedDisplay/Animations/All.hpp`
- `include/LedDisplay/Animations/Registry.hpp`
- `include/Support/CoreCommands.hpp`
- generated wire output from `make wire`
- host renderer registry output from `bin/led-render-build`
- `tools/led-render/examples/`
- `docs/animation-pipeline.md`
- `docs/commands.md`

Do not change layer semantics, presentation buffering, PubSub topic shape, GPU
ownership mapping, or the output backend for this work.

## Out Of Scope

- Runtime expression languages or shader parsers
- New dependencies
- Bitmap sprite support
- Heart, smiley, or other icon readability work
- Long-running FFT-reactive fields
- Feedback buffers, particles, reaction-diffusion, or other large stateful
  effects
- Per-animation heap allocation
- Per-pixel logging or metrics
- Backwards compatibility aliases for old generated artifacts or old `.tled`
  traces

## Shared Implementation Rules

- Keep `requiresFullFrame = false` unless a concrete animation proves otherwise.
- Prefer `Layer::Effect` for fire-and-forget effects that benefit from decay.
- Prefer `Layer::TransientEffect` for effects whose exact shape should clear
  every frame.
- Use `BlendOp::MaxValue` by default. Use `Replace` only for dense fills that
  must overwrite their own previous frame on a cleared layer.
- Keep configs trivially copyable and comfortably below the command payload
  limit.
- Use `uint8_t`, `uint16_t`, and `uint32_t` fixed-point phase math in render
  loops.
- Compute per-frame constants once before iterating pixels.
- Avoid per-pixel `float`, `double`, `sin`, `cos`, `pow`, `sqrt`, dynamic
  containers, or dynamic allocation.
- Keep angular frequency low. On 16 spokes, `4` and `8` fold patterns are very
  visible and should be intentional.
- Use the annular coordinate helpers when an effect depends on geometric
  distance or angle.
- Use strip-local ring coordinates when an effect is explicitly about travel
  along the LED strips.
- Manual commands should look useful on a quiet bench with no audio input.

## Per-Animation Integration Checklist

For each animation:

1. Add the animation directory with `Config.hpp`, `Animation.hpp`,
   `Command.hpp`, and `CommandDesc.hpp`.
2. Add an `AnimationKind` enum member.
3. Add the animation to `Animations::Payload`.
4. Add decode and construction handling in `Animations::makePayload()`.
5. Add the include to `Animations/All.hpp`.
6. Add the `/anim <name>` subcommand to `CoreCommands.hpp`.
7. Run `make wire`; do not hand-edit `include/Generated/Wire/`.
8. Run `bin/led-render-build`; do not hand-edit
   `tools/led-render/generated/AnimationRegistry.hpp`.
9. Add at least one host-render JSON example.
10. Render the example to `.tled`.
11. Capture at least one radial preview frame.
12. Update `docs/animation-pipeline.md` and `docs/commands.md`.
13. Build all active environments after generated wire changes:
    `bin/build -e master -e media -e gpu0 -e gpu1 -e io`.

## Suggested Implementation Order

1. `Starburst`: closest to `CenterWave`; validates the command and preview flow.
2. `Vortex`: first dense polar field; validates fixed-point phase shaping.
3. `Shutter`: first folded angular mask; validates aperture-style geometry.
4. `OrbitRing`: first compact moving lobe; validates angular/radial distance
   kernels.
5. `Lighthouse`: simple, useful, and related to existing spoke diagnostics.
6. `BreathingRings`: calm full-surface pulse and transition primitive.
7. `RadialCurtain`: distinct slanted wavefront using per-spoke phase offsets.
8. `PolarLattice`: reusable standing-wave field, also useful as a debug visual.
9. `Cymatic`: richer interference field after the simpler kernels are proven.
10. `Bolt`: jagged deterministic path; tune last because readability depends
    heavily on the 16-spoke surface.

The first implementation batch should be `Starburst`, `Vortex`, `Shutter`, and
`OrbitRing`. That gives four distinct visual families without introducing
larger state or shared architecture changes.

## Iteration Status

- Implemented as normal compiled animations: `Starburst`, `Vortex`, `Shutter`,
  `OrbitRing`, `Lighthouse`, `Cymatic`, `BreathingRings`, `RadialCurtain`,
  `PolarLattice`, and `Bolt`.
- A small shared helper lives in
  `include/LedDisplay/Animations/detail/SimpleField.hpp` for fixed-point
  progress, pulse, contrast, and distance kernels used by several effects.
- The first pass keeps each animation stateless and host-renderable. Radial
  preview tuning already raised the default `Vortex` brightness/width, shortened
  `OrbitRing` trails so the default reads as orbiting comets rather than a
  continuous ring, and changed `Bolt` from an unbounded random walk to a bounded
  zig-zag path around a seed spoke.

## Animation Designs

### Starburst

Purpose: a wave-like burst whose per-spoke width and travel speed create clear
radial points. This is the procedural "wave becomes a star" effect.

Default layer: `TransientEffect`

Default lifetime: `1200 ms`

Suggested command:

```text
/anim starburst [durationMs] [hue] [value] [rise] [peak] [wake] [points] [pointGain] [twist] [cycles]
```

Suggested config:

```cpp
struct WIRE_MSG StarburstConfig {
    uint8_t hue = 32;
    uint8_t saturation = 255;
    uint8_t value = 220;
    uint8_t rise = 1;
    uint8_t peak = 2;
    uint8_t wake = 6;
    uint8_t points = 4;
    uint8_t pointGain = 2;
    uint8_t twist = 0;
    uint8_t cycles = 1;
};
```

Render model:

- Use the `CenterWave` leading-edge profile as the base.
- For each spoke, derive a point scale from folded angular phase:
  `pointScale = triangle(points * theta + twist * radial)`.
- Increase `peak` and optionally travel distance by `pointScale * pointGain`.
- `cycles` controls how many burst passes happen during the lifetime.
- Keep the first version monochrome except for global hue offset.

Acceptance:

- `points = 4` reads as a four-point burst, not a quarter-turn artifact.
- `points = 8` remains readable and does not collapse into every-other-spoke
  flicker.
- `twist = 0` produces straight points.
- Nonzero `twist` visibly curves the points without creating discontinuities at
  the angular seam.

### Vortex

Purpose: a rotating spiral field that fills the annulus and reads well from a
distance.

Default layer: `Effect`

Default lifetime: `2400 ms`

Suggested command:

```text
/anim vortex [durationMs] [hue] [value] [arms] [twist] [width] [cycles] [hueStep]
```

Suggested config:

```cpp
struct WIRE_MSG VortexConfig {
    uint8_t hue = 160;
    uint8_t saturation = 255;
    uint8_t value = 220;
    uint8_t arms = 3;
    uint8_t twist = 5;
    uint8_t width = 128;
    uint8_t cycles = 1;
    uint8_t hueStep = 24;
};
```

Render model:

- Iterate logical pixels with `FieldCoordinates::forEachLogicalPixel()`.
- Build a phase from angle, strip radius, and time:
  `phase = theta * arms + stripRadius * twist + progress`.
- Convert the phase to a bright band with `sine8Q8` or `triangle8Q8`.
- Apply `smoothstep8` around `width` so arms are broad enough for 16 spokes.
- Use `hue + radialHue + ctx.hueOffset`, where `radialHue` is derived from
  `hueStep * stripRadius`.

Acceptance:

- Default settings show multiple broad spiral arms.
- `arms = 1` reads as one rotating spiral, not a full-surface blink.
- `arms > 5` should either be clamped or documented as intentionally aliasy.
- Viewer capture shows smooth radial continuity across both GPU ownership
  halves.

### Shutter

Purpose: aperture blades opening, closing, and rotating across the annulus.
This replaces the earlier `Iris` naming idea.

Default layer: `TransientEffect`

Default lifetime: `1600 ms`

Suggested command:

```text
/anim shutter [durationMs] [hue] [value] [segments] [openPct] [edgeWidth] [rotationCycles] [mode]
```

Suggested config:

```cpp
struct WIRE_MSG ShutterConfig {
    uint8_t hue = 48;
    uint8_t saturation = 255;
    uint8_t value = 210;
    uint8_t segments = 8;
    uint8_t openPct = 128;
    uint8_t edgeWidth = 48;
    uint8_t rotationCycles = 1;
    uint8_t mode = 0;
};
```

Render model:

- Fold angle into `segments` using `FieldMath::foldedAngle()`.
- Interpret the folded angle as distance to a blade edge.
- `openPct` is the static aperture openness. If `mode` requests animation,
  modulate openness over the lifetime with a triangle profile.
- `edgeWidth` controls the bright blade edge softness.
- `rotationCycles` rotates the blade phase during the lifetime.
- Fill only the blade edge and adjacent glow by default; a full opaque shutter
  fill may look too heavy with `MaxValue` blending.

Acceptance:

- `segments = 4`, `8`, and `16` each produce recognizable aperture structure.
- The default does not look like a debug spoke sweep.
- Rotation is smooth in the radial viewer and does not imply wheel input.
- The command and docs consistently use `shutter`, not `iris`.

### OrbitRing

Purpose: one or more comet-like lobes orbit around a selected radial band.

Default layer: `Effect`

Default lifetime: `2400 ms`

Suggested command:

```text
/anim orbit [durationMs] [hue] [value] [radius] [radialWidth] [angularWidth] [comets] [laps] [trail] [sparkle] [hueJitter] [radialDrift] [radialDirection]
```

Suggested config:

```cpp
struct WIRE_MSG OrbitRingConfig {
    uint8_t hue = 96;
    uint8_t saturation = 255;
    uint8_t value = 220;
    uint8_t radius = 128;
    uint8_t radialWidth = 36;
    uint8_t angularWidth = 28;
    uint8_t comets = 2;
    uint8_t laps = 1;
    uint8_t trail = 56;
    uint8_t sparkle = 220;
    uint8_t hueJitter = 24;
    uint8_t radialDrift = 48;
    uint8_t radialDirection = 1;
};
```

Render model:

- Treat `radius` as strip-local Q0.8 radial center.
- Compute radial pulse with `ringPulseQ8`.
- If `trail = 0`, compute a symmetric angular pulse around each comet center.
- If `trail` is nonzero, compute a directional comet envelope with a soft
  leading edge and a fading wake behind the motion direction. Avoid combining a
  symmetric head with a separate directional trail; that splits into multiple
  sweep heads on the 16-spoke surface.
- Apply deterministic per-LED sparkle to trail pixels only. `sparkle` controls
  the brightness randomization strength and `hueJitter` controls the raw hue
  deviation width.
- `radialDrift` moves the radial center during the animation;
  `radialDirection = 0` keeps the selected lane fixed, `1` travels outward, and
  `2` travels inward.
- The final scale is `radialScale * angularScale`.

Acceptance:

- Default shows two clear orbiting lobes on one radial band.
- `comets = 1` reads as a single orbiting object.
- `radialWidth` and `angularWidth` are wide enough by default to survive
  output brightness scaling.

### Lighthouse

Purpose: a show-worthy rotating beam, distinct from the debug `SpokeSweep`.

Default layer: `Effect`

Default lifetime: `3000 ms`

Suggested command:

```text
/anim lighthouse [durationMs] [hue] [value] [beamWidth] [trailSpokes] [cycles] [innerRing] [outerRing]
```

Suggested config:

```cpp
struct WIRE_MSG LighthouseConfig {
    uint8_t hue = 144;
    uint8_t saturation = 255;
    uint8_t value = 220;
    uint8_t beamWidth = 3;
    uint8_t trailSpokes = 4;
    uint8_t cycles = 1;
    uint8_t innerRing = 0;
    uint8_t outerRing = 0;
};
```

Render model:

- Derive the current beam center from elapsed time and `cycles`.
- Draw a directional angular envelope around the current spoke. Clamp the
  effective beam width to at least three spokes because one- and two-spoke heads
  alias into split sweeps on the current 16-spoke surface.
- Apply radial gating from `innerRing` to `outerRing`; `outerRing = 0` means
  the full strip.
- Apply a fading spoke trail behind the current beam using angular distance in
  the direction of motion. `trailSpokes = 0` disables the intentional trail,
  leaving only edge anti-aliasing.

Acceptance:

- Default reads as one rotating light beam with a visible tail.
- `trailSpokes = 0` is still useful as a clean beam.
- It is visually richer than `SpokeSweep` and should not replace the diagnostic.

### Cymatic

Purpose: wave interference from a few virtual sources, inspired by cymatics but
implemented as a cheap deterministic field.

Default layer: `Effect`

Default lifetime: `3200 ms`

Suggested command:

```text
/anim cymatic [durationMs] [hue] [value] [sourceMode] [wavelength] [speed] [contrast] [hueStep]
```

Suggested config:

```cpp
struct WIRE_MSG CymaticConfig {
    uint8_t hue = 176;
    uint8_t saturation = 240;
    uint8_t value = 180;
    uint8_t sourceMode = 0;
    uint8_t wavelength = 36;
    uint8_t speed = 96;
    uint8_t contrast = 180;
    uint8_t hueStep = 16;
};
```

Render model:

- Use a fixed small source set selected by `sourceMode`: opposite spokes,
  triangle, square, or rotating pair.
- Avoid `sqrt` by using approximate distance from source to pixel:
  `max(abs(dx), abs(dy)) + min(abs(dx), abs(dy)) / 2`.
- Sum two to four `sine8Q8(distance * wavelength - time)` terms.
- Apply `contrast` with a threshold or smoothstep.
- Keep source count fixed and small.

Acceptance:

- Default shows stable interference bands, not random sparkle.
- `sourceMode` variants are visibly different in the local viewer.
- Render cost remains comfortably below budget before adding more sources.

### BreathingRings

Purpose: multiple broad rings expanding, contracting, or pulsing in place.

Default layer: `TransientEffect`

Default lifetime: `2400 ms`

Suggested command:

```text
/anim rings [durationMs] [hue] [value] [spacing] [width] [cycles] [direction] [hueStep]
```

Suggested config:

```cpp
struct WIRE_MSG BreathingRingsConfig {
    uint8_t hue = 112;
    uint8_t saturation = 255;
    uint8_t value = 170;
    uint8_t spacing = 8;
    uint8_t width = 3;
    uint8_t cycles = 1;
    uint8_t direction = 0;
    uint8_t hueStep = 8;
};
```

Render model:

- Use strip-local radial phase.
- `direction = 0` expands outward, `1` contracts inward, `2` breathes in place.
- Ring centers are generated by `spacing` and shifted by elapsed phase.
- Draw broad `ringPulseQ8` bands across all spokes.
- Add `hueStep` per ring for a subtle color ladder.

Acceptance:

- Default reads as calm, full-surface movement.
- It should be less visually aggressive than `Starburst` and `Vortex`.
- Width and spacing defaults should avoid sparse single-ring output.

### RadialCurtain

Purpose: a slanted wavefront that crosses rings with per-spoke phase offsets,
like a fan or fabric ripple.

Default layer: `Effect`

Default lifetime: `2600 ms`

Suggested command:

```text
/anim curtain [durationMs] [hue] [value] [width] [tilt] [speed] [outerOrigin] [spokePhase]
```

Suggested config:

```cpp
struct WIRE_MSG RadialCurtainConfig {
    uint8_t hue = 200;
    uint8_t saturation = 220;
    uint8_t value = 190;
    uint8_t width = 4;
    uint8_t tilt = 32;
    uint8_t speed = 128;
    bool outerOrigin = false;
    uint8_t spokePhase = 16;
};
```

Render model:

- Compute a scan front similar to `SineWave`.
- Add a per-spoke phase offset from `tilt` or `spokePhase`.
- The front can originate at the inner or outer edge.
- Draw a broad lobe around the front and optionally let `Effect` layer decay
  provide the wake.

Acceptance:

- Default is clearly different from `SineWave`; it should look angularly
  slanted or folded.
- `outerOrigin` works without duplicating the full `SineWave` trace machinery.

### PolarLattice

Purpose: crossed radial and angular standing waves, useful as both a simple
show effect and a visual sanity check for field math.

Default layer: `TransientEffect`

Default lifetime: `2400 ms`

Suggested command:

```text
/anim lattice [durationMs] [hue] [value] [radialMode] [angularMode] [speed] [mix] [contrast]
```

Suggested config:

```cpp
struct WIRE_MSG PolarLatticeConfig {
    uint8_t hue = 64;
    uint8_t saturation = 255;
    uint8_t value = 170;
    uint8_t radialMode = 4;
    uint8_t angularMode = 3;
    uint8_t speed = 96;
    uint8_t mix = 128;
    uint8_t contrast = 160;
};
```

Render model:

- Combine an animated radial wave and a fixed angular standing wave.
- `mix = 0` means mostly rings, `255` means mostly spokes, middle values
  create a lattice.
- Keep the angular phase stable by default. Let `speed` move the radial phase
  through the fixed angular lattice instead of rotating both fields against each
  other.
- Apply `contrast` after mixing so the pattern is crisp on low brightness.

Acceptance:

- `angularMode = 3` default avoids accidental 4-fold quarter-turn repetition.
- Explicit `angularMode = 4` is allowed but should be documented as a strong
  fourfold pattern.
- `mix` extremes remain useful as simple ring or spoke fields.

### Bolt

Purpose: a deterministic jagged radial crack/lightning path with a glow.

Default layer: `Effect`

Default lifetime: `900 ms`

Suggested command:

```text
/anim bolt [durationMs] [hue] [value] [width] [jitter] [forks] [seed] [outerOrigin]
```

Suggested config:

```cpp
struct WIRE_MSG BoltConfig {
    uint8_t hue = 24;
    uint8_t saturation = 255;
    uint8_t value = 255;
    uint8_t width = 1;
    uint8_t jitter = 1;
    uint8_t forks = 1;
    uint8_t seed = 0;
    bool outerOrigin = true;
};
```

Render model:

- Select a seed spoke, then derive a bounded per-segment offset and long bend
  from deterministic hashes of `seed` and the ring segment.
- Interpolate between offsets every few rings so the path is jagged but stays
  connected and local instead of wrapping around the full 16-spoke circle.
- Draw a core at full value and one or two neighboring spokes as glow.
- `forks` adds short secondary paths for only part of the radial length.
- Keep the hash deterministic so host and firmware traces match exactly.

Acceptance:

- Default is readable as a connected path, not scattered random pixels.
- The path should change only at intentional phase steps, not every 8 ms frame.
- `seed` produces stable variant shapes for repeatable preview captures.

## Host Preview Plan

For every animation, add one default example and one stress/tuning example under
`tools/led-render/examples/`.

Suggested filenames:

- `starburst-default.json`
- `starburst-eight-point.json`
- `vortex-default.json`
- `vortex-wide.json`
- `shutter-default.json`
- `shutter-sixteen-segment.json`
- `orbit-ring-default.json`
- `lighthouse-default.json`
- `cymatic-default.json`
- `breathing-rings-default.json`
- `radial-curtain-default.json`
- `polar-lattice-default.json`
- `bolt-default.json`

Standard preview commands:

```sh
bin/led-render --config tools/led-render/examples/vortex-default.json --output /tmp/vortex.tled
bin/led-analyze summary /tmp/vortex.tled --stats
bin/led-analyze flicker /tmp/vortex.tled --hue-ping-pong
bin/led-view /tmp/vortex.tled --layout radial --scale 10 --spacing 1.35 --bloom
bin/led-view /tmp/vortex.tled --capture /tmp/vortex.png --frame 80 --layout radial --bloom
```

For tuning, compare at least these cases in the radial viewer:

- default global brightness preview
- `--brightness 96`
- first frame, middle frame, and last frame
- 16-spoke seam behavior
- both `Effect` and `TransientEffect` layer behavior when the animation could
  plausibly use either

## Firmware Validation Plan

After each small batch:

1. Generate wire support with `make wire`.
2. Regenerate host renderer registry with `bin/led-render-build`.
3. Build all active environments:

   ```sh
   bin/build -e master -e media -e gpu0 -e gpu1 -e io
   ```

4. Preview host examples before upload.
5. If hardware validation is needed, upload `master`, `gpu0`, and `gpu1`, then
   manually publish one command at a time from the master console.
6. Check `ledDisp.slow` and `ledDisp.miss` after testing the heaviest example.

Manual command examples will be added to `docs/commands.md` after the concrete
argument order is implemented.

## Completion Criteria

The simple animation set is complete when:

- All planned animations can be launched through `/anim`.
- Each animation has a host-render example.
- Each animation is documented in `docs/animation-pipeline.md`.
- Command arguments are documented in `docs/commands.md`.
- Generated wire and host registry outputs are current.
- `bin/build -e master -e media -e gpu0 -e gpu1 -e io` passes.
- The local radial viewer shows each default effect as a distinct visual family.
- No animation adds large per-slot state, heap allocation, or a pipeline-level
  dependency.
