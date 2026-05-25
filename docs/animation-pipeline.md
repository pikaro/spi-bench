# Animation Pipeline

This is the current design reference for GPU-side LED animation rendering.
Older port and legacy notes have been removed from the active documentation to
avoid carrying migration-era assumptions forward.

The pipeline is intentionally not backwards compatible with the first bring-up
implementation or with the old `../led` codebase. That code was used only as an
architectural reference. The objective here is the clean final shape for the
current firmware.

## Current Status

The embedded pipeline is feature-complete for current animation development:

- animation commands are generic `Play`, `Update`, `Stop`, hue-offset, and
  rotation-offset requests
- `Display` owns lifecycle, strobe accounting, present buffering, and output
  presentation only
- `AnimationEngine` owns command draining, animation slots, input snapshots,
  layer rendering, and concrete animation dispatch
- concrete animations live under `include/LedDisplay/Animations/`, one header
  per animation
- reusable drawing access lives under `include/LedDisplay/Primitives/`
- rendering uses scratch -> layer -> composed frame
- present buffering supports `None`, `Double`, `Triple`, and `Quadruple`;
  the current default is `Triple`
- FFT and wheel data are latest-value input streams for animations, not
  display-level rendering special cases
- the persistent wheel indicator starts through the same generic command path
  as all other animations

Remaining work is feature growth and visual tuning, not another structural
replacement:

- decide the final FFT visual policy; `FftReactive` is still a placeholder
- add richer real animations and the primitives they need
- add host-side render tooling from
  [animation-pipeline-offline-render-plan.md](animation-pipeline-offline-render-plan.md)
- tune layer decay, opacity, and blend choices with hardware and offline traces
- revisit parallel per-layer rendering only when larger LED counts prove the
  sequential renderer is the bottleneck

## Responsibilities

`Display` is the hardware presentation owner. It initializes the output backend,
subscribes to generic animation command PubSub, delegates input subscriptions to
the engine, handles the present-strobe ISR, records present timing, and calls
`FastLedOutput::show()` with complete frames. It must not know about concrete
animations such as waves, FFT visuals, or the wheel indicator.

`AnimationEngine` is the animation backend. It validates and queues commands,
drains them at frame boundaries, owns active slots, captures latest FFT and
wheel inputs, renders each active animation into scratch, blends scratch into
the target layer, composes layers into the output frame, and expires timed
animations.

`Animations/*` files own animation-specific semantics. Each base animation file
contains the config payload, default style/layer/lifetime, command helper
declarations, and render/update behavior for that animation. Per-animation
`*Commands.hpp` headers in the same directory contain the command helper
definitions; they are intentionally not centralized in one command factory. The
registry is boring glue: it maps `AnimationKind` to payload construction, style
lookup, update dispatch, and render dispatch.

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
updates affect pixels.

`src/master/led_bringup.hpp` owns the master-triggered LED bring-up probes. It
publishes generic animation commands after boot so GPU mapping and long-running
indicator startup use the same command path as runtime orchestration.
Automatic master runtime orchestration is held until the spoke probe's publish
delay plus lifetime has elapsed. The bring-up sweep must run against an
untransformed logical-to-physical map; otherwise normal effects such as wheel
rotation, beat waves, or future brightness modulation can obscure topology
diagnostics. Manual `/anim` commands remain direct diagnostics.
The spoke probe is intentionally delayed long enough for the high-speed
SPI/PubSub links to recover after a full-system reset; publishing it too early
can race subscriber availability and make the diagnostic disappear.

The umbrella topology maps logical spokes in four-spoke quadrants. Within each
quadrant, logical spoke order is mirrored onto the physical wire segment order:
logical segment `0` maps to physical segment `3`, `1` to `2`, `2` to `1`, and
`3` to `0`. Radial serpentine direction is still derived from the physical
segment because that follows the actual LED strip wiring.

## Frame Flow

Each GPU frame follows this path:

1. The present strobe wakes the display task.
2. Buffered modes present the newest complete frame first.
3. The display asks the engine to render the next complete frame.
4. The engine drains queued commands.
5. The layer stack clears or decays layer buffers according to layer policy.
6. The engine snapshots latest FFT and wheel input state.
7. Each active animation renders into shared scratch.
8. Scratch is blended into the animation's target layer using the animation's
   style.
9. Layers are composed in fixed order into the present buffer render target.
10. Timed animations expire after composition.

Command and PubSub paths use the same engine:

- command console helpers publish `AnimationCommand` over PubSub
- master orchestration publishes the same command payloads
- GPUs subscribe to the animation command topic and enqueue the command
- `Play` creates a slot, `Update` mutates matching active slots, and `Stop`
  disables matching slots

Updates are matched by animation kind and optional request ID. Request ID `0`
means update every active animation of that kind. Misses are counted in
`ledDisp.updMiss`.

## Layers

Layers are fixed and composed in enum order:

- `Background`
- `Fft`
- `Effect`
- `Wheel`
- `Debug`

Current default policies:

- `Background`: cleared each frame, `MaxValue`, full opacity
- `Fft`: persistent decay, `MaxValue`, full opacity
- `Effect`: persistent decay, `MaxValue`, full opacity
- `Wheel`: cleared each frame, `Alpha`, partial opacity
- `Debug`: persistent decay, `AddValue`, full opacity

These defaults are tuning values. The ownership boundary is the important part:
backgrounds, FFT, one-shot effects, wheel overlays, and diagnostics never fight
inside a single shared frame buffer.

## Current Animations

`CenterWave` is a one-shot effect on the `Effect` layer. Its config has
separate rise, peak, and wake ring counts.

`DiagnosticFill` is a debug-layer fill used for command/output validation.

`SpokeSweep` is a debug-layer bring-up animation. Master starts it after boot to
walk logical spokes and mark radial direction, so mapping errors are visible
without giving `Display` or the output backend any animation-specific API.
It is also available manually through `/anim sweep`; use `trailSpokes=0` when
checking physical spoke order.

`FftReactive` is a placeholder FFT-driven visual on the `Fft` layer. It
consumes the engine's latest FFT snapshot and renders a fallback gradient when
no FFT frame exists. The final FFT design is intentionally undecided.

`WheelIndicator` is a long-running wheel-layer animation with default request
ID `1`. It renders a small spoke group from the latest wheel position and
accepts update commands for its display parameters. If no wheel state has been
received yet, it renders at position zero so the layer is still testable.
Master starts the persistent wheel indicator only after the bring-up spoke
probe window has completed.

## Buffering And Timing

The present strobe currently runs at 125 Hz. `LedPipelineBounds::targetFps` is
also 125, giving an 8 ms work budget for the GPU's present-and-render step.
This is separate from FastLED temporal dithering. FastLED's measured-FPS
threshold remains an output-backend invariant and must not be changed to hide
pipeline timing failures.

`PresentBufferMode` is selected at compile time:

- `None`: render and present from one frame owner
- `Double`: render and present frames are distinct
- `Triple`: render, ready, and presenting storage are available; this is the
  default
- `Quadruple`: available for future experiments with deeper buffering

The current renderer is single-task and sequential. Future 4-8x LED-count work
may need distributed per-layer rendering: one worker per layer renders into
layer-local scratch, then a composition task gates on completed layers. Current
animation code should keep rendering through supplied canvases and avoid
assuming that only one task will ever render layers, but the present
implementation should stay sequential until metrics justify the extra stacks,
barriers, and failure modes.

The pipeline does not diagnose frame health by comparing task wake-to-task wake
spacing against 8 ms; that measurement includes ISR and scheduler jitter. The
actionable timing diagnostic is the measured LED frame step: selected-buffer
presentation plus rendering the next complete buffer. Over-budget frame work is
counted as `ledDisp.slow`, while actual missed external strobes are counted as
`ledDisp.miss`.

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
- `slow`: frame steps whose measured work exceeded the 8 ms budget
- `active`: active animation slot count
- `rndMax`: maximum observed render duration in microseconds
- `showMax`: maximum observed output-show duration in microseconds
- `stepMax`: maximum observed present-and-render step duration in microseconds

Validation on 2026-05-25 with `master`, `gpu0`, `gpu1`, and `io` attached
showed both GPUs receiving FFT-driven animation traffic and the forced
`/anim wheel-update 160 96 3 1 1` update through master. `gpu0` and `gpu1`
reported zero queue, command, render, show, input, repeat, and missed-strobe
failures. The wheel update incremented `update` to `1` on both GPUs with
`updMiss=0`, proving that the persistent wheel indicator received the generic
update. Observed maxima were about 2.5 ms render and 2.6 ms show, comfortably
below the 8 ms work budget for the current LED count. Later validation should
also require `slow=0` and a `stepMax` comfortably below 8000 us.

The final local state after adding `SpokeSweep`, master bring-up publication,
and `slow`/`stepMax` metrics has been build-validated but not revalidated on
hardware. Device validation was paused on 2026-05-25 after the USB devices
disappeared from the host. Do not treat the final state as hardware-validated
until the nodes enumerate reliably again and the validation commands below pass.

## Known Visual Bugs

Closely spaced waves have previously produced isolated pink/white flicker when
different hues followed each other immediately. Do not close or root-cause this
by live visual inspection alone. It may be pipeline blending, FastLED
conversion, temporal dithering, timing, power, or physical LED behavior.

After the offline renderer exists, reproduce this with deterministic command
scripts and compare raw layer buffers, final HSV frames, converted RGB frames,
and hardware output. Until then it remains a known visual issue, even if the
new layer/composition behavior changes the symptom.

## Validation Commands

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
