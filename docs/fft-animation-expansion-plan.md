# FFT Animation Expansion Plan

## Purpose

Add several long-running FFT-driven animations that can run on `Fft` and
`FftAlt` and be faded by the master. The current single FFT animation is a good
first real field, but it is no longer enough as the only target for layer
crossfades.

This plan is intentionally scoped to compiled, project-owned C++ animations.
Do not add a runtime preset language, shader parser, new dependency, or broad
animation pipeline rewrite.

## Assumptions

- The active LED surface remains 16 spokes by 46 radial samples.
- Each GPU renders only its owned pixels through the existing logical-to-local
  map.
- FFT animations use the existing GPU-side audio controls: smoothed
  bass/mid/high/energy plus bass/mid/high attack values.
- `Fft` and `FftAlt` remain layer names. They describe the crossfade-capable
  layer role, not a specific animation.
- Backwards compatibility is not a requirement. Remove stale names, commands,
  generated files, examples, and docs instead of keeping aliases.

## Naming

Rename the current `FftReactive` animation to `SpectralWeave`.

Rationale:

- `FftReactive` is too generic once multiple FFT animations exist.
- The current effect is a woven polar field of radial, angular, and
  high-frequency components, so `SpectralWeave` describes the concrete visual.
- The old name should not survive as `AnimationKind::FftReactive`,
  `FftReactiveConfig`, `/anim fft`, host-render animation name, generated wire
  header, or example config.

Planned command names:

- `/anim weave` for the renamed existing animation
- `/anim iris` for `SpectralIris`
- `/anim sparks` for `OrbitSparks`
- `/anim cells` for `StainedCells`

## Scope

Implementation touches:

- `include/LedDisplay/Animations/<Name>/`
- `include/LedDisplay/Animations/Registry.hpp`
- `include/LedDisplay/Animations/All.hpp`
- `include/LedDisplay/Interfaces/AnimationKind.hpp`
- `include/Support/CoreCommands.hpp`
- `src/master/orchestration.hpp`
- `include/Generated/Wire/`
- `tools/led-render/generated/AnimationRegistry.hpp`
- `tools/led-render/examples/`
- `docs/animation-pipeline.md`
- `docs/commands.md`

Do not change layer semantics, the animation engine queue model, PubSub topic
shape, or GPU ownership mapping unless a concrete implementation blocker is
found.

## Current Implementation Facts

Use these as implementation anchors:

- `include/LedDisplay/detail/AnimationEngine.hpp` drains animation commands at
  frame boundaries, snapshots FFT/peak/wheel inputs, updates one shared
  `Primitives::AudioControlSmoother`, renders active animations into scratch,
  blends scratch into the target layer, then composes layers.
- `AudioControlSmoother` already reduces the eight-band `FftFrame` into
  `bass`, `mid`, `high`, `energy`, `bassAttack`, `midAttack`, and
  `highAttack`. New FFT animations should use `ctx.audio`; do not decode or
  smooth raw FFT bands again inside each animation.
- `include/LedDisplay/Primitives/FieldCoordinates.hpp` provides logical
  annulus coordinates through `forEachLogicalPixel()`, `fieldPoint()`,
  `theta`, `stripRadius`, `physicalRadius`, `x`, and `y`.
- `include/LedDisplay/Primitives/FieldMath.hpp` already has the right low-cost
  helpers: `scale8`, `qadd8`, `average2`, `average3`, `triangle8Q8`,
  `sine8Q8`, `smoothstep8`, `ringPulseQ8`, `foldedAngle`, and `palette2`.
- `Canvas::pixel()` and `Canvas::sample()` already route logical
  `(spoke, radial)` positions through the logical-to-local map, so animations
  should draw in logical coordinates and let ownership filtering happen there.
- `AnimationCommand` payloads are fixed at
  `LedDisplayConfig::animationCommandPayloadBytes`, currently 32 bytes. Keep
  config structs trivially copyable and well under that limit.
- `Animations::Payload` is a `std::variant` in every animation slot. Do not add
  large buffers to any animation struct, because one large alternative increases
  storage for every slot.
- `Fft` and `FftAlt` have persistent decay and `MaxValue` layer blending. This
  is useful for long-running field trails and sparse spark trails.
- The layer fade-swap command already enables the target layer, interpolates
  opacity, disables the faded-out layer at completion, and stops animations on
  that faded-out layer. Master orchestration should rely on this behavior.
- The host renderer discovers animations by directory convention from
  `include/LedDisplay/Animations/*/Animation.hpp` plus sibling `Config.hpp`.
  Firmware dispatch still needs manual updates in
  `include/LedDisplay/Animations/Registry.hpp`.

## Source Edit Checklist

For every new animation:

1. Add `Config.hpp` with `WIRE_MSG <Name>Config` and `<Name>Spec`.
2. Add `Animation.hpp` with static traits, config storage, and
   `render(AnimationRenderContext &ctx)`.
3. Add `Command.hpp` with `<Name>Command::makeCommand(...)`.
4. Add `CommandDesc.hpp` with the CLI parser and subcommand descriptor.
5. Add the animation kind to `AnimationKind`.
6. Add the animation type to `Animations::Payload`.
7. Add decode handling in `Animations::makePayload()`.
8. Add the command descriptor include and subcommand entry in
   `include/Support/CoreCommands.hpp`.
9. Add or update a host-render example under `tools/led-render/examples/`.
10. Update `docs/commands.md` and `docs/animation-pipeline.md`.

Keep this mechanical. Do not use this work as an excuse to redesign the
registry or command surface. The only acceptable registry improvement during
this task would be a narrow helper that reduces repeated decode boilerplate
without changing behavior.

## Shared Implementation Rules

- Default `requiresFullFrame` should be `false` for all four FFT animations.
  They draw through `Canvas` and do not need direct access to the entire output
  frame.
- Default `defaultLayer` should be `Layer::Fft`.
- Default lifetime should be `0` for long-running FFT animations.
- Default style should normally be `BlendOp::Replace` for dense fields and
  `BlendOp::MaxValue` for sparse splats. The target `Fft`/`FftAlt` layer then
  composes with its layer policy.
- Include exactly what each header uses. Animation headers should generally
  include only their own config, render context, primitive helpers, renderer
  helpers, and standard headers needed for fixed-size math.
- Use `uint8_t` and `uint16_t` phase math in hot paths. `uint32_t` is fine for
  time multiplication before shifting back down.
- Compute per-frame audio-derived constants once before pixel or particle
  loops.
- Do not use per-pixel `float`, `double`, `sin`, `cos`, `sqrt`, `pow`, dynamic
  allocation, containers with runtime allocation, or logging.
- Keep angular modes low. On 16 spokes, fixed 4-fold patterns read as
  90-degree repeats and can look like a control-path jump. Any 4-fold or
  8-fold choice should be intentional and visible in the config.
- Prefer deterministic fallback fields when no audio input has arrived, so
  manual commands remain testable on a quiet bench.

## Battery-Conscious Workflow

Implementation may happen on battery. Do not compile continuously.

Use lightweight checks during ordinary editing:

```sh
rg -n "FftReactive|/anim fft|fftReactive" include src docs tools
rg -n "SpectralWeave|SpectralIris|OrbitSparks|StainedCells" include src docs tools
git diff --check
git status --short
```

Treat these as compile or generator milestones, not per-edit checks:

- `make wire PIO_ENV=master`
- `bin/led-render-build`
- `bin/build -e <env>`
- full multi-environment `bin/build`
- hardware upload or monitor validation

Use the smallest meaningful milestone build first. For animation-only code,
`gpu0` is usually the first firmware compile target because both GPU
environments share `src/gpu/` and the animation engine. When command
orchestration or wire shape changes are included, include `master` at the same
milestone. Run the full `master`, `media`, `gpu0`, `gpu1`, and `io` build only
at the final integration milestone or before handing off a branch for review.

Recommended milestone gates:

1. Rename gate: after `FftReactive` is fully renamed and generated artifacts
   are refreshed.
2. Standalone animation gate: after all three new animation directories and
   host examples exist.
3. Master sequencing gate: after master preset/crossfade orchestration is
   wired.
4. Final integration gate: after docs, examples, generated files, and manual
   command surfaces are updated.

If battery is tight, stop after a clean lightweight check and leave the next
milestone commands explicit in the work log. Do not start a long compile just
to prove a small intermediate edit.

## Animation Designs

### SpectralWeave

This is the renamed existing FFT animation.

Keep the current basic behavior: a polar FFT field that combines radial,
angular, and high-band components. Smoothed bass/mid/high values modulate the
field shape and spectral balance modulates hue.

Implementation work:

1. Move `include/LedDisplay/Animations/FftReactive/` to
   `include/LedDisplay/Animations/SpectralWeave/`.
2. Rename config, command, command desc, spec, animation struct, log text, and
   generated references.
3. Update the command from `/anim fft` to `/anim weave`.
4. Update host-render examples to use `SpectralWeave`.
5. Keep the existing field math narrow. Do not refactor the effect while
   renaming unless required by the new names.

Concrete rename checklist:

- `AnimationKind::FftReactive` -> `AnimationKind::SpectralWeave`
- `FftReactiveConfig` -> `SpectralWeaveConfig`
- `FftReactiveSpec` -> `SpectralWeaveSpec`
- `FftReactive` -> `SpectralWeave`
- `FftReactiveCommand` -> `SpectralWeaveCommand`
- `handleFftReactiveCommand` -> `handleSpectralWeaveCommand`
- `fftReactiveSubcommand` -> `spectralWeaveSubcommand`
- `Generated/Wire/Totem__LedDisplay__Animations__FftReactiveConfig.hpp`
  should disappear after regeneration
- host-render generated names and examples should use `SpectralWeave`
- docs should describe `/anim weave`, not `/anim fft`

Keep the existing `decorrelatedHighAngularMode()` behavior during the rename.
The old high-band hue path once created a fixed 4-cycle pattern with default
`angularMode = 3`; do not regress to a direct `angularMode + 1` term.

### SpectralIris

A clean polar aperture and mandala field. Visually, this should read as an
opening iris, soft radial shutter, petal mask, and rim glow.

Audio mapping:

- Bass opens and closes the radial aperture.
- Mid rotates or folds the petal field.
- High sharpens the rim and adds narrow bright accents.
- Spectral balance shifts hue.

Performance shape:

- Full-field render over logical pixels.
- Use `foldedAngle`, `ringPulseQ8`, `smoothstep8`, `scale8`, `qadd8`, and
  integer phase math.
- Avoid per-pixel float, `sin`, `cos`, `sqrt`, `pow`, or multi-octave noise.
- Keep angular symmetry low, usually 4 or 8, because 16 spokes alias quickly.

Expected config fields:

- `baseHue`
- `saturation`
- `value`
- `baseValue`
- `petals`
- `aperture`
- `rimWidth`
- `contrast`
- `peakSensitivity`
- `flowSpeed`
- `hueModulation`

Suggested defaults:

- `baseHue = 96`
- `saturation = 255`
- `value = 220`
- `baseValue = 8`
- `petals = 8`
- `aperture = 128`
- `rimWidth = 28`
- `contrast = 180`
- `peakSensitivity = 96`
- `flowSpeed = 24`
- `hueModulation = 128`

Implementation sketch:

1. Precompute `baseHue + ctx.hueOffset`, shaped `bass`, `mid`, `high`,
   `energy`, and peak accents.
2. Compute a slow `phaseQ8` from `ctx.clock.nowMs * flowSpeed`.
3. For each `FieldPoint`, fold `theta` with `petals`, then compute a petal mask
   from folded angle and radius. Keep this to 8-bit triangle/sine helpers.
4. Compute an aperture center from `aperture` plus bass modulation.
5. Use `ringPulseQ8(point.stripRadius, apertureCenterQ8, rimWidthQ8)` for the
   bright rim.
6. Use high energy and high attack to sharpen or brighten the rim, not to add
   extra high-frequency angular modes.
7. Combine `baseValue`, aperture fill, rim, petal mask, and audio scale with
   saturating math.
8. Hue should move slowly: base hue plus spectral balance plus a small radial
   or folded-angle offset scaled by `hueModulation`.

Concrete math notes:

- `petals` should be clamped to `[1, 8]`.
- `petalTheta = FieldMath::foldedAngle(point.theta + phase, petals)`.
- A cheap petal mask can start as `triangle8(petalTheta)` or
  `sine8Q8(petalTheta << 8)`, shaped by `smoothstep8`.
- `apertureCenterQ8` can be
  `(aperture << 8) + (scale8(bass, 64) << 8)`, clamped to the strip radius
  range.
- `rimWidthQ8` should clamp to at least one radial sample so the rim does not
  vanish.
- `value = baseValue + scale8(config.value, field)` with `qadd8`.
- If `!ctx.audio.hasInput`, use a low fallback energy and slow phase so the
  animation still renders but does not look like a full-energy show state.

Guardrail: if the result looks like spoke blinking, reduce `petals` or remove
the angular high-band contribution before adding more smoothing.

### OrbitSparks

A sparse FFT-driven particle and trail animation. Visually, this should read as
orbiting embers, spectral sparks, or firefly-like points moving around and
through the annulus.

Audio mapping:

- Bass changes radial push or orbit radius.
- Mid changes angular drift.
- High controls sparkle value and particle density.
- Attack values briefly brighten splats.

Performance shape:

- Sparse render, not a full-field effect.
- Use deterministic particles derived from `seed`, `index`, and time. Do not
  store particle state in the animation payload or add a frame-history buffer.
- Draw small local splats with `Canvas::pixel`.
- Let `Fft`/`FftAlt` persistent layer decay provide trails.
- Keep default particle count modest, likely 24 to 48. Clamp hard if a command
  requests more.

Expected config fields:

- `baseHue`
- `saturation`
- `value`
- `sparkCount`
- `sparkSize`
- `orbitSpeed`
- `radialDrift`
- `highSparkle`
- `peakSensitivity`
- `seed`
- `hueModulation`

Suggested defaults:

- `baseHue = 32`
- `saturation = 255`
- `value = 230`
- `sparkCount = 32`
- `sparkSize = 1`
- `orbitSpeed = 32`
- `radialDrift = 96`
- `highSparkle = 160`
- `peakSensitivity = 128`
- `seed = 0xA5`
- `hueModulation = 160`

Implementation sketch:

1. Clamp `sparkCount` to a compile-time maximum, likely 48.
2. For each spark index, build a cheap deterministic hash from `seed`, index,
   and maybe a small time phase. This replaces stored particle state.
3. Compute angular phase from index spacing plus `nowMs * orbitSpeed`.
4. Compute radial phase from a second hash plus bass/mid-modulated drift.
5. Convert to nearest logical `spoke` and `radial`. This is sparse, so direct
   logical integer placement is enough; do not attempt subpixel particles in
   the first pass.
6. Draw one pixel or a tiny plus-shaped splat depending on `sparkSize`.
7. Scale value by high energy and high attack for sparkle. Bass can push radial
   position outward; mid can change angular drift direction or speed.
8. Let `Fft`/`FftAlt` layer decay create trails. Do not render a private trail
   history.

Concrete math notes:

- `theta = hash + phase + index * (256 / sparkCount)` is sufficient for the
  first pass.
- `spoke = (theta * Config::spokeCount) >> 8`.
- `radial = ((radialPhase * Config::ringCount) >> 8)`, clamped to
  `Config::ringCount - 1`.
- Bass can add an outward offset before the radial clamp.
- `sparkSize = 0` should draw one pixel. `sparkSize = 1` can draw center plus
  immediate radial neighbors. Avoid larger defaults until measured.
- Use `BlendOp::MaxValue` for splats so overlapping sparks do not dim one
  another.

Useful helper additions, if needed:

- A tiny `hash8(uint8_t a, uint8_t b, uint8_t c)` helper in `FieldMath`.
- A `wrapSpoke()` helper if not already duplicated locally.
- A `clampRadial()` helper or local inline function.

Guardrail: because this is sparse, do not compensate by increasing particle
count aggressively. If it feels too empty, tune splat size, layer decay, and
value first.

### StainedCells

A low-seed Voronoi/Worley membrane field. Visually, this should read as stained
glass, cracked crystal, or living cell borders.

Audio mapping:

- Bass swells cell interiors.
- Mid moves seed phase or membrane drift.
- High brightens borders.
- Spectral balance colors cell families.

Performance shape:

- Full-field render with a small bounded seed count.
- Use squared distance only. Do not use `sqrt`.
- Start with 5 to 7 seeds. Do not allow many moving seeds until measured.
- Keep cells large; small cell sizes will alias on 16 spokes.
- Prefer deterministic seed positions from config seed plus time phase.

Expected config fields:

- `baseHue`
- `saturation`
- `value`
- `baseValue`
- `seedCount`
- `borderWidth`
- `interiorValue`
- `driftSpeed`
- `contrast`
- `peakSensitivity`
- `seed`
- `hueModulation`

Suggested defaults:

- `baseHue = 160`
- `saturation = 245`
- `value = 210`
- `baseValue = 10`
- `seedCount = 6`
- `borderWidth = 28`
- `interiorValue = 64`
- `driftSpeed = 16`
- `contrast = 180`
- `peakSensitivity = 96`
- `seed = 0x3D`
- `hueModulation = 144`

Implementation sketch:

1. Clamp `seedCount` to a small maximum, likely 7.
2. For each frame, derive seed positions in logical polar space from config
   seed, seed index, and slow phase. Seed derivation should happen inside the
   render function with fixed-size `std::array` or direct recomputation.
3. Represent positions as `theta` in 0..255 and `radius` in 0..255 or Q8.
4. For each pixel, find nearest and second-nearest seed using squared distance.
   Angular distance must wrap. Radial distance can clamp.
5. Use `F2 - F1` or a bounded approximation as the border signal. Do not use
   `sqrt`.
6. Bass raises `interiorValue` or expands cells. High raises border value.
   Mid shifts seed drift phase.
7. Hue can come from nearest seed family plus spectral balance. Keep
   hue variation broad enough that cells read as distinct.

Concrete math notes:

- Define a local `Seed { uint8_t theta; uint8_t radius; uint8_t hueOffset; }`.
- `thetaDistance = FieldMath::angularDistance(point.theta, seed.theta)`.
- `radiusDistance = abs(point.stripRadius >> 8 - seed.radius)`.
- Start with
  `distance = thetaDistance * thetaWeight + radiusDistance * radiusWeight`
  squared or multiplied into a `uint32_t`. Exact physical accuracy matters less
  than stable, non-aliasing cells.
- Track `nearest`, `secondNearest`, and nearest seed index.
- Border strength can start from
  `border = smoothstep8(clampU8((secondNearest - nearest) >> shift))`, inverted
  if necessary after host preview.
- Keep seed drift slow. If cell borders crawl too fast, the 16-spoke surface
  will read as flicker rather than organic motion.

Distance guidance:

- Scale angular distance down enough that one spoke step does not dominate all
  radial structure. A simple starting point is to compute angular distance in
  0..255, radial distance in 0..255, then weight angular distance by physical
  radius or a conservative constant.
- If that is too costly, use unweighted polar distance first and inspect the
  host radial view before optimizing.

Guardrail: this is the most CPU-expensive candidate. Implement it last and
measure during a two-FFT-layer crossfade before adding more seeds or motion.

## Command And Config Details

Each animation should follow the existing directory shape:

- `Config.hpp`: `struct WIRE_MSG <Name>Config` and `<Name>Spec`
- `Animation.hpp`: `struct <Name>` with `<Name>Config config{}`
- `Command.hpp`: `<Name>Command::makeCommand(...)`
- `CommandDesc.hpp`: CLI parser and subcommand descriptor

The CLI parser should mirror the current optional-argument pattern:

- Use `detail::optionalU32(...)` for optional numeric arguments.
- Support `-` only through the existing optional-arg helper behavior.
- Clamp numeric config values with existing `clampU8` and `clampU16` helpers.
- Parse optional `Layer` as the final argument for each FFT animation so manual
  staging on `FftAlt` remains easy.

Suggested command argument order:

- Common prefix: `durationMs`, `hue`, `value`, `baseValue`
- Shape parameters
- Audio sensitivity parameters
- `hueModulation`
- `layer`

This keeps manual commands consistent while still allowing each animation to
have distinct shape controls.

Generated artifact handling:

- Do not manually maintain `include/Generated/Wire/` headers. Refresh them at a
  milestone with `make wire PIO_ENV=master`.
- Do not manually maintain
  `tools/led-render/generated/AnimationRegistry.hpp`. Refresh it with
  `make led-render-registry` or `bin/led-render-build`.
- If stale generated files refer to `FftReactive`, remove them by regeneration,
  not by adding aliases.
- Internal `.tled` traces are not compatibility artifacts. Regenerate traces
  after animation, geometry, or host-render changes.

## Master Crossfade Orchestration

Replace the single master `FftFieldMapping` with a small FFT visual preset
table. Each preset stores:

- animation kind
- animation-specific config values or enough inputs for a local preset builder
- preferred layer at launch time
- stable request ID for `Fft`
- stable request ID for `FftAlt`
- minimum dwell time
- fade duration

The master should keep a small state machine:

1. Start the first preset on `Fft` after the bring-up gate.
2. Keep `Fft` active at opacity 255 and `FftAlt` disabled or opacity 0.
3. When the next preset is due, enable the hidden layer and set its opacity to
   0.
4. Publish the next animation to the hidden layer with that layer's stable
   request ID.
5. Publish `/layer swap Fft FftAlt <durationMs>` through the existing layer
   fade command.
6. After the GPU-local fade completes, the faded-out layer is disabled and
   animations on that layer are stopped by the existing engine behavior.
7. Flip the master's active/hidden layer bookkeeping.

Do not add a new crossfade wire command unless the existing layer swap command
proves insufficient.

Concrete master data shape:

- `enum class FftVisualKind { SpectralWeave, SpectralIris, OrbitSparks,
  StainedCells };`
- `struct FftVisualPreset` with kind, dwell, fade, and a short name for logs.
  A preset can either hold a small tagged union of configs or let
  `publishFftPreset()` switch on the kind and construct the config locally.
  Prefer the simpler shape that keeps `src/master/orchestration.hpp` readable.
- `struct FftLayerRuntime` with active layer, hidden layer, active preset
  index, last switch time, fade-in-progress flag if needed, request IDs for
  each FFT layer, and suppression state.

Keep the preset table in `src/master/orchestration.hpp` as plain C++
config-as-code. Do not create a shared semantic config layer.

Request ID guidance:

- Use a stable request ID per FFT layer, not per preset.
- Starting a new animation on the hidden layer should replace any old hidden
  animation on that same layer without touching the visible layer.
- Example shape: active `Fft` request ID 3, `FftAlt` request ID 4. The exact
  numbers are not important, but they must not collide with wheel or debug
  persistent request IDs.

Manual stop behavior should stay diagnostic-friendly:

- Stop-all suppresses automatic FFT refresh and rotation until restart.
- A stop targeting either FFT-layer request ID also suppresses automatic FFT
  refresh and rotation until restart.
- Manual `/anim weave`, `/anim iris`, `/anim sparks`, or `/anim cells`
  commands remain direct diagnostics and do not need compatibility aliases.

Sequencing edge cases:

- If a layer swap is already in progress, do not stage another preset.
- If the hidden layer is not opacity 0, explicitly publish opacity 0 before
  staging.
- If the hidden layer is disabled, publish layer active `on` before staging.
- If staging the hidden animation publish fails, do not start the fade. Retry
  later.
- If the fade command fails, leave the active layer untouched and retry later.
- After a successful fade request, do not assume immediate completion on the
  master. Use elapsed time plus fade duration as the master's bookkeeping
  window, while the GPU owns actual layer opacity.
- Keep logs concise: preset name, from layer, to layer, dwell, and fade
  duration are enough.

## Implementation Order

1. Rename `FftReactive` to `SpectralWeave`.
2. Run lightweight rename checks.
3. At the rename milestone, regenerate wire and host-render registry.
4. Render and inspect the renamed `SpectralWeave` example to prove no behavior
   changed unintentionally if host-render build budget is available.
5. Add `SpectralIris`.
6. Add `OrbitSparks`.
7. Add `StainedCells`.
8. Add master preset sequencing and automatic crossfade staging.
9. Update docs and examples.
10. Run milestone validation, with the full multi-environment build reserved for
   final integration.

This order keeps automatic crossfade sequencing until after each animation can
be rendered independently.

Per-step acceptance:

- After step 1, `rg -n "FftReactive|/anim fft|fftReactive"` should find no
  source references except possibly historical planning docs if deliberately
  left as history.
- After each new animation, `bin/led-render-build` should discover it without
  manual host dispatch edits.
- Before master sequencing, each animation should have one host example that
  renders in `pipeline` mode with synthetic audio.
- After master sequencing, there should be one host or manual command fixture
  that demonstrates `Fft` to `FftAlt` staging.

## Validation

Lightweight checks after ordinary edits:

```sh
rg -n "FftReactive|/anim fft|fftReactive" include src docs tools
rg -n "SpectralWeave|SpectralIris|OrbitSparks|StainedCells" include src docs tools
git diff --check
```

Rename milestone:

```sh
make wire PIO_ENV=master
make led-render-registry
bin/led-render-build
bin/led-render --config tools/led-render/examples/spectral-weave.json --output /tmp/spectral-weave.tled
bin/led-analyze summary /tmp/spectral-weave.tled --stats
bin/build -e master -e gpu0
```

Standalone animation milestone:

```sh
make led-render-registry
bin/led-render-build
bin/led-render --config tools/led-render/examples/spectral-iris.json --output /tmp/spectral-iris.tled
bin/led-render --config tools/led-render/examples/orbit-sparks.json --output /tmp/orbit-sparks.tled
bin/led-render --config tools/led-render/examples/stained-cells.json --output /tmp/stained-cells.tled
bin/led-analyze summary /tmp/spectral-iris.tled --stats
bin/led-analyze flicker /tmp/spectral-iris.tled --hue-ping-pong
bin/build -e gpu0
```

Add at least one multi-animation host-render sequence that stages two FFT
animations on `Fft` and `FftAlt` and fades between them.

Master sequencing milestone:

```sh
make wire PIO_ENV=master
make led-render-registry
bin/led-render-build
bin/build -e master -e gpu0
```

Final integration milestone:

```sh
make wire PIO_ENV=master
make led-render-registry
bin/led-render-build
bin/build -e master -e media -e gpu0 -e gpu1 -e io
git diff --check
```

Hardware validation is also a milestone, not a routine edit check:

- Upload `master`, `gpu0`, and `gpu1`.
- Confirm the master starts the first FFT preset after bring-up.
- Confirm a staged second preset fades on `FftAlt`.
- Watch display metrics for `slow`, `miss`, `stepMax`, and FastLED FPS
  recovery.
- During crossfade, confirm timing remains acceptable with both FFT layers
  active.

Runtime evidence to capture:

- Master logs for preset start, hidden-layer staging, fade request, and manual
  suppression.
- GPU logs for layer active/opacity/swap handling.
- `/metrics` or monitor output showing no sustained `slow` or missed strobe
  growth during crossfade.
- A short quiet-input test proving fallback fields render without FFT input.

## Implementation Status

Implemented in the current tree:

- `FftReactive` was renamed to `SpectralWeave` for active source, command, host
  render, and generated wire names.
- `/anim weave`, `/anim iris`, `/anim sparks`, and `/anim cells` are registered
  as the active FFT animation command surface.
- `SpectralIris`, `OrbitSparks`, and `StainedCells` were added under
  `include/LedDisplay/Animations/`.
- Master orchestration now rotates the managed FFT visual through weave, iris,
  sparks, and cells, staging the next preset on the hidden FFT layer and using
  a GPU-local `Fft`/`FftAlt` layer swap for crossfades.
- Host examples were added as:
  `spectral-weave-audio-sweep.json`, `spectral-iris.json`,
  `orbit-sparks.json`, and `stained-cells.json`.

Validation performed during implementation:

```sh
make led-render-registry
bin/led-render-build
bin/led-render --config tools/led-render/examples/spectral-weave-audio-sweep.json --output /tmp/spectral-weave.tled
bin/led-render --config tools/led-render/examples/spectral-iris.json --output /tmp/spectral-iris.tled
bin/led-render --config tools/led-render/examples/orbit-sparks.json --output /tmp/orbit-sparks.tled
bin/led-render --config tools/led-render/examples/stained-cells.json --output /tmp/stained-cells.tled
bin/led-analyze summary /tmp/spectral-weave.tled --stats
bin/led-analyze summary /tmp/spectral-iris.tled --stats
bin/led-analyze summary /tmp/orbit-sparks.tled --stats
bin/led-analyze summary /tmp/stained-cells.tled --stats
bin/build -e master -e gpu0
git diff --check
```

`make wire` was not rerun in this implementation pass because unrelated
wire-generator work was also active in the worktree. The generated FFT config
headers currently present in `include/Generated/Wire/` were sufficient for the
`master` and `gpu0` milestone builds to pass.

## Risks And Guardrails

- Do not add large state arrays to animation payload alternatives. A large
  variant alternative increases storage cost for every animation slot.
- Do not add compatibility for old `.tled` traces, old generated wire headers,
  or old animation names. Regenerate artifacts instead.
- Do not use full shader-style float math in per-pixel loops.
- Do not add a new persistent PlatformIO environment.
- Do not move show policy onto GPU nodes. The master owns preset selection and
  sequencing; GPU nodes execute commands.
- Do not treat visual artifacts as control-path bugs until the field geometry
  has been checked, especially fixed 4-fold or quarter-turn patterns on the
  16-spoke surface.
