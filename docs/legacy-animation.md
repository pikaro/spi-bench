# Legacy Animation Pipeline

This document describes the legacy animation pipeline found through
`legacy/include`, which currently points at `../led/upper/include`. The legacy
tree is not used as a source of implementation style for this repository. It is
used here to recover behavior, data flow, topology intent, and useful animation
concepts.

The main animation-related files are:

- `legacy/include/LedDisplay/LedDisplay.hh`
- `legacy/include/LedDisplay/LedDisplayConfig.hh`
- `legacy/include/LedDisplay/LedDisplayBackground.hh`
- `legacy/include/LedTopology/LedTopologyUmbrella.hh`
- `legacy/include/Interfaces/IAnimation.hh`
- `legacy/include/Animation/AnimationBase.hh`
- `legacy/include/Animation/AutoExpire.hh`
- `legacy/include/Animation/*.hh`
- `legacy/include/AnimationController/AnimationController.hh`
- `legacy/include/AnimationEngine/AnimationEngine.hh`
- `legacy/include/AnimationLayer/*.hh`
- `legacy/include/Compositor/*.hh`
- `legacy/include/LedDriver/*.hh`

`../led/upper/src/upper/main.cpp` shows how the pieces were wired together at
runtime.

## Runtime Wiring

The old `setup()` created one `LedTopologyUmbrella`, built a vector of LED
drivers, began the `LedDisplay`, created animation layers, started the display
task, and then started a persistent background animation.

The hardware driver was `LedDriverFastLED`. When WiFi was enabled, a second
`LedDriverWebSocket` mirrored rendered RGB frames to a websocket route for
browser visualization. Both drivers implemented the same `ILedDriver` contract.

The legacy layer order was:

- `Background`: priority 0, `MaxV`, high decay
- `Fft`: priority 10, `MaxV`, moderate decay
- `Effect`: priority 15, `MaxV`, light decay
- `Wheel`: priority 20, `MaxV`, light decay
- `Debug`: priority 25, `AddV`, stronger decay

The `LedDisplayBackground` component spawned `AnimTapestry` on the background
layer and used a FreeRTOS timer to advance its hue.

## Hardware Topology

`LedTopologyUmbrella` encoded the physical LED order:

- `wires = 4`
- `ledsPerWire = 184`
- `stripsPerWire = 4`
- `ledsPerStrip = 46`
- total pixels = `4 * 184 = 736`
- total strips/spokes = `4 * 4 = 16`
- rings = 46, one ring per LED offset along each spoke

Each wire carried four physical strips. Within each wire, strips were mapped in
a serpentine order: even segments were forward, odd segments were reversed.

The topology built two views over the same physical LED indices:

- `spokes`: 16 spans, each span containing the 46 physical indices of one
  spoke from center to edge
- `rings`: 46 spans, each span containing the 16 physical indices at the same
  radial distance across all spokes

The topology interface also exposed `loops()`, `bands()`, and named `region()`
lookups, but the umbrella implementation returned empty loop and band spans.
The `region("rings")` and `region("spokes")` calls returned the flattened
arena used to back the ring and spoke spans.

## Animation Model

Animations implemented `IAnimation`:

- `onAttach()`
- `onDetach()`
- `onEvent(eventId, payload)`
- `render(RenderContext&)`
- `setSpawnInfo(AnimSpawnInfo)`
- `config()`
- `isFinished(FrameClock)`
- `name()`
- `requestId()`
- `lifetimeMs(now)`

`AnimationBase<Config>` provided a default persistent animation with common
configuration, spawn metadata, name storage, and helpers such as `fade()`.
`AnimationConfig` carried per-animation opacity and blend operation.

`RenderContext` contained:

- `FrameClock { timeUs, frame }`
- `TopologyView { rings, spokes, loops, bands }`
- `target`: the scratch buffer for the target layer
- `count`: total pixel count

Animations wrote HSV pixels into `ctx.target` using physical pixel indices from
the topology spans or direct physical index loops.

## Requests And Lifetimes

`AnimationController` decoupled animation requests from the display task.
Callers posted a `SpawnSpec` containing:

- target layer handle
- lifetime policy
- factory callback returning `std::unique_ptr<IAnimation>`
- optional creation timeout

Requests were pushed into a fixed-capacity pointer ring
(`MpscPtrRing<AnimationRequest, 32>`), but each request and spawn spec was
heap-allocated before being pushed. When `waitForCreation` was true, the caller
waited on a FreeRTOS task notification until the display task serviced the
request and published the created handle.

Lifetime behavior was implemented by `AutoExpire`, a wrapper around an inner
animation:

- `Persistent`: never expires
- `DurationMs`: expires after a configured duration after first render
- `OneShot`: delegates completion to the inner animation

The controller serviced the request queue from the display task. It created the
animation through the factory, optionally wrapped it in `AutoExpire`, attached
it to the engine, set spawn metadata, and notified the waiting caller.

## Render And Compose Flow

`LedDisplay` owned:

- an `AnimationLayerStack`
- an `AnimationController`
- an `AnimationEngine`
- a final composed HSV buffer
- one or more `ILedDriver` instances

On each display task step:

1. Service pending animation requests.
2. Let the `AnimationEngine` render all attached animations into their layers.
3. Ask the layer stack for layers sorted by priority.
4. Compose ordered layers into the final HSV buffer.
5. Submit the final HSV buffer to each driver.

`AnimationEngine::render()` did the per-animation work:

1. Apply layer decay to all layers.
2. Clear disabled layers.
3. Build a `RenderContext` from the current clock and topology.
4. For each attached animation:
   - skip disabled layers
   - clear that layer's scratch buffer
   - render the animation into scratch
   - blend scratch into the layer buffer using the animation config
5. Remove finished animations after rendering.

This made each animation independent. Multiple animations could target the same
layer, and their scratch outputs were blended into the persistent layer buffer.

`Compositor::compose()` then cleared the final output buffer and blended each
enabled layer into it using the layer config. There were therefore two blending
stages:

- animation scratch into a layer, using animation opacity and blend op
- layer into final frame, using layer opacity and blend op

## Blend Operations

`BlendOp` supported:

- `MaxV`: keep the source hue/saturation only when source value is brighter
  than the destination value
- `AddV`: saturating add of value, keeping hue/saturation from the brighter
  contributor
- `Alpha`: linear interpolation of value, using source hue/saturation when the
  source contributes at least as much as the existing destination

The old pipeline used `CHSV` as the internal frame representation and converted
to `CRGB` only in the driver task.

## Driver And Buffering

`LedDriverBase` converted HSV frames into RGB and passed them to a concrete
driver implementation. It owned a frame inbox selected by `LedDriverBufferMode`:

- `Unbuffered`
- `DoubleBuffer`
- `TripleBuffer`

`LedDisplay` and `LedDriverBase` ran as separate FreeRTOS tasks. The display
task produced HSV frames and called `driver->present()`. The driver task polled
its inbox, converted HSV to RGB, and called `_onLoop()`.

`LedDriverFastLED` owned the FastLED `CRGB` array, configured four WS2812B
outputs, acquired ESP-IDF power-management locks, copied the converted RGB
buffer into the FastLED buffer, and called `FastLED.show()`.

The old LED pins were:

- `_PIN_LED_1 = GPIO_NUM_7`
- `_PIN_LED_2 = GPIO_NUM_8`
- `_PIN_LED_3 = GPIO_NUM_9`
- `_PIN_LED_4 = GPIO_NUM_10`

The old `LedDriverFastLED` hard-coded the four pins and four equal segments. It
also hard-coded the umbrella topology constants through includes.

`LedDriverWebSocket` reused the same converted RGB buffer and broadcast it as a
nanopb `LedData` payload. That was useful for visualization, but it was tied to
the old WiFi/web stack. It is documented here as legacy behavior only and is
not a GPU-node port target.

## Concrete Animations

The legacy animations were:

- `AnimTapestry`: subtle background, spatially decorrelated S/V breathing with
  a stable per-pixel hash and adjustable hue
- `AnimRingsRotate`: one rotating bright head per ring with a fading trail
- `AnimSpokesWipe`: one-shot spoke wipe from center outward with fading trails
- `AnimWheelIndicator`: highlights the spoke corresponding to a wheel angle
  event
- `AnimSpokeIndicator`: diagnostic spoke coloring with markers at endpoints and
  intervals
- `AnimFlatWhite`: diagnostic white fill on every fourth spoke
- `AnimIndexWalk`: single bright pixel walking through physical index order
- `AnimWhiteDots`: every second LED white, with a stale hard-coded expectation
  of 32 pixels
- `AnimSparkle`: placeholder with no implemented render behavior

Several animations used topology spans (`rings` and `spokes`) and are worth
preserving conceptually. The diagnostic animations are the valuable visual
reference. The production-looking animations should not be preserved visually,
and several diagnostics assumed the umbrella layout or old test setups
directly.
