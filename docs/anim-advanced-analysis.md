# anim-advanced analysis

This is an analysis-only review of [anim-advanced.md](anim-advanced.md)
against the current LED display pipeline. It intentionally does not propose a
code patch.

## Baseline constraints

- Current GPU targets are ESP32-S3 nodes using FastLED only at the output
  backend. Animations render project-local `HsvColor` frames, then
  `FastLedOutput` converts HSV to FastLED `CRGB` and calls `FastLED.show()`.
- The logical LED surface is 16 spokes by 46 radial samples, 736 LEDs total.
  Each active GPU owns two of four physical groups, so each GPU renders 368
  local LEDs.
- The GPU frame cadence is 125 Hz, with an 8 ms budget for presenting the
  selected frame and rendering the next one.
- The renderer is sequential: one scratch buffer is cleared, one active
  animation renders into it, scratch is blended into that animation's layer,
  and layers are composed into the output frame.
- Layers currently exist for `Background`, `Fft`, `Effect`, `Wheel`, and
  `Debug`. `Fft` and `Effect` have persistent decay; `Background` and `Wheel`
  clear every frame.
- GPU animation inputs currently include latest FFT frame and wheel state.
  Beat events are consumed on master and turned into animation commands; GPU
  animations do not currently receive beat events directly.
- Animation command payloads are fixed at 32 bytes, and active animation
  payloads are stored inside a `std::variant` in every animation slot. Large
  state arrays must not be placed in ordinary animation payload types, because
  one large variant alternative increases the storage cost of every slot.

## FastLED compatibility

Most of the outline is compatible with the current FastLED setup because the
ideas are rendering algorithms, not output protocols. The useful boundary is:

- Effects should render into project `HsvColor` buffers through `Canvas` or
  future primitives.
- The output backend should remain responsible for FastLED conversion,
  dithering, brightness, and `show()`.
- Reusable procedural helpers should avoid including FastLED directly unless
  they are intentionally renderer-specific. Keeping helpers project-owned also
  keeps the generic/offline renderer path viable.

FastLED is therefore not the blocker. The real constraints are CPU budget,
fixed RAM, per-GPU ownership, and the current layer/state model.

FastLED temporal dithering remains a timing constraint. The current backend
tracks measured FPS and expects recovery to at least 100 FPS. Heavier effects
that push the frame step over budget can degrade both visual timing and
dithering behavior.

## Hardware math implications

ESP32-S3 has a single-precision FPU, but double-precision math is still
software-emulated on Espressif FPUs. The ESP32-S3 family also has vector
instructions aimed at neural-network and DSP workloads, but the current LED
code does not deliberately use those instructions. Treat plain C++ procedural
animation code as scalar unless a focused DSP/SIMD helper is added later.

Practical rules for this project:

- `float` is acceptable for low-rate parameter updates and setup-time
  precomputation.
- Avoid `double` in hot paths. Unsuffixed math calls such as `sin`, `cos`,
  `pow`, and `exp` can promote work to double; use lookup tables, fixed-point
  math, or explicit `sinf`/`cosf` only where measured safe.
- Do not run per-LED trig, multi-octave noise, or convolution-heavy logic at
  125 Hz without a benchmark.
- Use fixed-point or 8-bit math for most per-pixel work. The visual resolution
  is low enough that high numeric precision will not be the limiting factor.

References:

- Espressif FPU overview:
  https://developer.espressif.com/blog/2025/10/cores_with_fpu/
- Espressif ESP32-S3 overview:
  https://www.espressif.com/en/products/socs/esp32s3/docs

## Shared foundations that are useful

The strongest part of the outline is the shared field model. It fits the
current architecture if implemented as small primitives and fixed state, not as
a runtime expression engine.

### 1. Coordinate abstraction

This is the first useful piece to build.

Current code has `LogicalPoint { spoke, radial }` and `Canvas` helpers for
pixel, ring, spoke, and radial line drawing. It does not yet expose normalized
polar or Cartesian coordinates.

A compatible path is to add a topology/display coordinate helper that maps each
logical LED to compact coordinates:

- `spoke`, `radial`
- strip-local radius, preferably fixed-point such as Q0.16
- world radius from the geometric center, accounting for the physical center
  gap
- angle as an 8-bit or 16-bit turn value
- optional `x` and `y` as Q1.15 or similar fixed-point values

This should be precomputed or derived from small lookup tables. With only 16
spokes, a 16-entry sine/cosine table plus a 46-entry strip-radius table is
enough for most effects. The radius table must model the annulus: production
strips start at roughly 60 mm radius and run about 417 mm, so radial index zero
is the inner visible ring, not the center. A
full 736-entry coordinate LUT is also plausible, but it should be justified
against RAM and offline-render reuse.

This single abstraction supports polar FFT fields, kaleidoscope folding, SDF
rings, Voronoi cells, particle splats, flow fields, and feedback sampling.

### 2. Audio conditioning

The outline's `bass`, `mid`, `treb`, RMS, onset, and tempo-phase concepts are
useful, but they should not be raw per-frame values read directly from the
wire frame.

Current GPU inputs provide eight scaled FFT bands. A small GPU-side audio
conditioning struct could derive:

- bass envelope from sub-bass and bass bands
- mid envelope from low-mid, mid, and high-mid bands
- high envelope from presence, brilliance, and air bands
- overall energy/RMS proxy
- simple attack/release smoothed values
- optional onset impulse derived locally from envelope deltas

This avoids requiring direct GPU beat subscription for the first pass. If later
effects need exact media beat events on GPU, adding beat input to
`AnimationEngine` is a separate shared input change and should be treated as a
small design step rather than hidden inside one animation.

### 3. Field math primitives

Useful primitives:

- 8-bit or fixed-point `smoothstep`
- ring pulse profiles
- cosine or hand-authored HSV palettes
- angular fold/kaleidoscope fold
- low-cost value noise and maybe limited fBM
- nearest-neighbor sampling from a previous local frame
- splat/add kernels for particles and sprites

The initial toolkit should be deliberately small. The goal is not to port
Book-of-Shaders machinery; it is to provide reusable kernels that match a
16-by-46 radial LED surface.

### 4. Stateful storage policy

Several proposed effects need state: feedback buffers, reaction-diffusion
grids, Turing fields, particles, boids, Voronoi seeds, or attractor traces.

Do not store large state inside animation payload variants. Better options:

- one dedicated fixed buffer owned by `AnimationEngine` or `LayerStack` for
  feedback-style effects
- a singleton scene/state object for heavy field algorithms
- small per-animation state only for bounded counts, such as particles or seed
  lists
- reduced update rates for simulation state, with cheap sampling every LED
  frame

This is the main architecture caveat in the outline. The visual ideas fit, but
their state must be placed carefully.

## Per-idea assessment

### MilkDrop/projectM mental model

Useful as a concept: per-frame parameters plus per-pixel field rendering driven
by audio. Not useful as a literal implementation target.

Do not build a runtime equation engine, preset parser, or projectM-compatible
system now. That would add parser/runtime complexity, dynamic expression
evaluation, and unbounded performance behavior. The compatible version is a
small set of compiled C++ field animations with curated parameters.

### Polar feedback warp

High visual value, medium implementation risk.

Compatible with current layers if implemented as a dedicated animation or
field pass that owns a previous-frame buffer. It can reuse coordinate
abstractions, palette helpers, audio smoothing, and sampling primitives.

Main caveat: each GPU currently stores only its owned pixels. A seamless
feedback sampler over all 16 spokes is not available locally. Initial versions
should use nearest-neighbor local sampling, avoid large angular displacements
across GPU ownership boundaries, and accept possible seams. A full cross-GPU
previous-canvas exchange is not worth considering for the first version.

Implementation shape:

- Add a previous owned-frame buffer or reuse a controlled persistent layer.
- Sample by logical spoke/radial where the sampled point maps to an owned local
  pixel.
- Clamp or wrap radial/angle cheaply.
- Use fixed-point angle/radius offsets.
- Start with one feedback field on `Fft` or `Background`, not many instances.

Verdict: worth doing after coordinate helpers and audio smoothing.

### Domain-warped noise / fBM

Useful, but only in a reduced embedded form.

Low-frequency noise fields can make a good background or FFT layer. Full
shader-style fBM with several noise octaves, float coordinate math, and
per-pixel domain warping at 125 Hz is risky.

Implementation path:

- Use fixed-point or 8-bit noise.
- Precompute or update a coarse field at a lower rate, then sample it.
- Keep octave count low, likely 1-2 at first.
- Use audio to modulate palette, threshold, drift, and contrast rather than
  recomputing expensive geometry.

Verdict: good background candidate if deliberately cheap.

### Polar kaleidoscope / mandala fields

Highly compatible and cheap.

This mostly needs angle folding, radial functions, palettes, and low-frequency
time/audio parameters. It reuses the same coordinate abstraction as the
feedback and SDF ideas.

The outline correctly warns about angular resolution. With 16 spokes, angular
frequencies above about 5-8 will alias or just become spoke blinking. Favor
4-fold or 8-fold symmetry and slow parameter morphs.

Verdict: good early implementation, especially for FFT/background.

### Reaction-diffusion / Gray-Scott

Potentially valuable, but isolated and state-heavy compared with the simpler
field ideas.

A compact 16-by-46 simulation is plausible with 8-bit or 16-bit fixed-point
state and a reduced update rate. A float, double-buffered, full-rate simulation
is the wrong starting point.

Implementation cautions:

- It needs two chemical fields and likely double buffering.
- State should not live in the animation payload variant.
- Run simulation at 15-30 Hz first, not necessarily every 125 Hz LED frame.
- Use cheap boundary handling: wrap theta, clamp or reflect radial.
- Keep injection simple and audio-driven.

Verdict: promising later, after field primitives and metrics exist.

### Multi-scale Turing patterns

Artistically relevant but more expensive than Gray-Scott if implemented
naively.

Multiple neighborhood averages at multiple radii become expensive on a small
MCU unless optimized. On a 16-by-46 polar grid, it may be possible with
carefully chosen separable/ring-local approximations or low update rates, but
it is substantial standalone work.

Verdict: defer. Do not implement before simpler stateful fields prove the
budget.

### Flow-field particles with trails

Good fit.

Particles are cheaper than per-pixel simulations when counts stay modest. A
32-96 particle system with fixed-point positions, velocities, and splats can
render into `Effect`, `Fft`, or `Background` layers. It reuses coordinate math,
noise/field lookup, splat kernels, and layer decay.

This also aligns well with sprite-based stars/hearts: the same spawn, movement,
and splat/draw primitives can handle both abstract particles and occasional
recognizable sprites.

Verdict: high value and compatible.

### Boids / flocking

Useful only if kept small.

The main risk is O(n^2) neighbor search. For 32 agents this is likely fine; for
96 agents it needs measurement or spatial bucketing. Literal boid rendering is
not the right aesthetic; trails and density splats are the useful part.

Verdict: optional later particle variant, not a first field effect.

### SDF shapes

Very compatible if used for abstract radial geometry.

Rings, apertures, iris/shutter masks, radial petals, halos, and soft outlines
can be computed cheaply from radius and angle. This should not require a
general SDF library. Use it as a structure/mask primitive over noise or FFT
fields.

Verdict: good early primitive family.

### Voronoi / Worley cells

Compatible with low seed counts.

On this surface, use large cells and maybe 5-12 seeds. Per-pixel distance to a
small seed list is fine; per-pixel distance to many moving seeds is not. Avoid
small cell sizes because 16 angular samples will alias badly.

Verdict: good medium-cost background/mask effect.

### Wave interference / cymatics

Very compatible.

Standing radial/angular waves and a few moving sources are cheap and map
naturally to the umbrella shape. This is likely one of the best replacements
for the current placeholder FFT visual because it can be driven directly by
the eight FFT bands.

Verdict: strong early candidate for `FftReactive` evolution.

### Strange attractors and parametric traces

Compatible as particle/trail effects, not as dense per-pixel formulas.

Use a few virtual traces, slowly morph parameters, and splat into a decaying
layer. Avoid expensive functions and chaotic coefficient changes every frame.

Verdict: good later effect-layer accent.

### Lenia / continuous cellular automata

Impractical as an early target.

Lenia-like convolution and parameter tuning are too much standalone work for
the current visual pipeline stage. It also needs state placement, update-rate
control, and tooling for parameter search.

Verdict: defer indefinitely unless there is a specific artistic requirement.

## Compatibility between ideas

Most useful ideas share the same foundation:

- coordinate LUT/abstraction
- audio envelope smoothing
- palette and shaping helpers
- fixed-point field math
- splat and local sampling primitives
- layer composition and decay

Good shared group:

- polar FFT fields
- wave interference
- kaleidoscope/mandala
- SDF masks
- Voronoi with few seeds
- domain-warped noise lite
- feedback warp

Particle/sprite shared group:

- flow-field particles
- attractor traces
- stars/hearts/twinkles
- small boid-like traces

More isolated group:

- Gray-Scott reaction-diffusion
- multi-scale Turing
- Lenia

These need simulation-specific buffers and update logic. They can still render
through the same canvas and layers, but they should not drive the initial
abstraction design.

## Sprite coexistence

Sprites can coexist cleanly with field animations.

The current layer model already supports the intended split:

- `Background`: continuously generated slow fields that clear every frame
- `Fft`: audio-reactive fields with decay or feedback
- `Effect`: one-shot sprites, particles, stars, hearts, twinkles, flashes
- `Wheel`: wheel indicator overlay
- `Debug`: topology and diagnostics

Spawning stars and hearts on `Effect` while using polar/wave/noise fields for
`Fft` and `Background` is compatible with the architecture. The important
detail is to keep sprites as accents. If sprite effects and field effects are
both on `Effect`, they will compete through the same layer decay and blend
policy. Prefer fields on `Background`/`Fft` and sprites on `Effect`.

A future dedicated `Sprite` layer might be useful, but adding a layer changes a
shared enum and wire-facing command semantics. It is not necessary for the
first implementation pass.

## Recommended implementation order

1. Add coordinate abstractions and iteration helpers.
   This is the common base for almost every useful idea.

2. Add GPU-side audio conditioning.
   Convert the latest FFT frame into smoothed bass/mid/high/energy values and
   local onset impulses.

3. Build cheap field primitives.
   Include palettes, shaping, angular folds, ring pulses, simple wave
   functions, and splat kernels.

4. Replace or extend `FftReactive` with a polar FFT field.
   Best candidates are wave interference, standing radial/angular modes, SDF
   masks, and low-frequency kaleidoscope folding.

5. Add an effect-layer particle/sprite primitive.
   This enables stars/hearts/twinkles and flow-field particles without
   disturbing background/FFT work.

6. Add a constrained feedback warp.
   Start local-only, nearest-neighbor, and one field instance. Measure
   `ledDisp.rndMax`, `showMax`, `stepMax`, `slow`, and missed strobes.

7. Consider stateful organic simulations.
   Gray-Scott is the first reasonable candidate. Turing and Lenia should wait
   until tooling and budget are proven.

## What not to do first

- Do not port MilkDrop/projectM or build a runtime equation language.
- Do not add heavy state arrays to ordinary animation payload variants.
- Do not use full shader-style float/fBM code per LED at 125 Hz.
- Do not rely on cross-GPU previous-frame data for feedback.
- Do not add new persistent PlatformIO environments or new dependencies for
  these effects without a separate design decision.
- Do not expand this into a broad animation rewrite. The current pipeline is
  already structurally suitable; the useful work is primitives, state
  placement, and carefully measured effects.

## Summary judgment

The outline is directionally useful if interpreted as "compiled procedural
fields with memory" rather than "port a shader/MilkDrop ecosystem." The best
near-term value is:

- polar coordinate helpers
- FFT envelope smoothing
- wave/interference FFT visuals
- kaleidoscope/SDF masks
- low-cost noise backgrounds
- particles/sprites on the effect layer
- one carefully bounded feedback warp

The risky parts are runtime formula engines, high-octave shader math,
cross-GPU feedback sampling, and large stateful simulations placed in the
wrong storage layer.
