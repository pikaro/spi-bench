# Legacy Animation Assessment

This document classifies the useful and non-useful parts of the legacy
animation pipeline. It is intentionally separate from
`legacy-animation.md`, which describes how the old implementation worked.

## Well-Designed And Reusable Concepts

The following concepts should be retained, but reimplemented in the current
repository style.

### Request/Execution Decoupling

Animation requests were posted from outside the render task and consumed by the
display task. This is the right boundary. It prevents unrelated application code
from mutating active animation state while a frame is being rendered.

The current repo should keep this idea, but use static, trivially-copyable queue
items like `LedPwm` does. Request payloads should be bounded variants or small
parameter structs, not heap-allocated factories.

### Separate Animation Classes

The old code kept each effect in its own class with a small config struct and a
single render entry point. This is useful for reviewability, testing by
inspection, and later topology-specific specialization.

The class model should remain, but the runtime ownership model should change:
active animation slots should store concrete animation variants in fixed
storage. New animation types can be added by extending the variant and slot
visitor, matching the existing `LedPwm::Animation` pattern.

### Topology-Aware Rendering

The most valuable old abstraction was that animations operated on topology
views instead of hard-coding every physical index. The umbrella topology exposed
spokes and rings over a single physical LED sequence, allowing effects such as
ring rotation, spoke wipes, and wheel indicators to be described in logical
terms.

This should be retained and made compile-time/static. Future topologies should
expose a common minimal view, and animations should state which topology
features they require.

### Layered Composition

The two-stage model was good:

1. each animation renders into scratch and blends into its target layer
2. ordered layers blend into the final frame

This lets persistent backgrounds, FFT overlays, one-shot effects, wheel
indicators, and debug overlays coexist without direct coupling. Layer priority,
opacity, decay, enablement, and blend mode are worth retaining.

The new implementation should use fixed arrays for layers and frame buffers,
and hot loops should iterate only LEDs owned by the current GPU node.

### HSV Internal Color

Using HSV internally is convenient for the existing animations. Hue rotation,
value trails, subtle background breathing, and diagnostic color markers are all
natural in HSV.

The FastLED platform layer may still convert HSV to RGB immediately before
showing the frame. The rest of the pipeline should not depend directly on
FastLED types unless the component intentionally chooses FastLED-compatible
color structs as its public representation.

## Conceptually Good But Flawed

These pieces had the right general intent but should not be ported directly.

### `IAnimation` Runtime Polymorphism

The interface made animations easy to describe, but every active animation was
owned through `std::unique_ptr<IAnimation>`. That creates heap traffic and
indirection in a hot path.

Replacement: use concrete animation classes plus a bounded
`std::variant<...>` active slot. Dispatch can use `std::visit` outside the
innermost pixel loops.

### Heap-Allocated Spawn Factories

`AnimationController` allocated `SpawnSpec` and `AnimationRequest`, stored a
`std::function`, and created animations through `std::make_unique`. This was
flexible but unsuitable for this project.

Replacement: define queue-copyable animation commands. A request should contain
layer, lifetime, request ID if needed, and a bounded animation payload variant.

### Dynamic Layer And Topology Storage

`AnimationLayer`, `AnimationLayerStack`, and `LedTopologyUmbrella` used
`std::vector`, `std::unique_ptr`, `std::unordered_map`, and dynamic sorting.
The sizes are all known from topology and static config.

Replacement: use `std::array`, compile-time layer count, compile-time topology
tables, and fixed ordered layer lists. If layer priority is configurable, sort
once during `begin()` into fixed storage.

### Handle Stability

Animation handles were just indices into a vector. Detaching an entry erased
from the vector and shifted later entries, invalidating handles. The code tried
to validate handles by scanning live entries, but direct indexing with a stale
index remained fragile.

Replacement: fixed slots with generation counters or request IDs. Removing an
animation marks a slot inactive and increments generation; it must not move
other active slots.

### Display Task And Driver Task Split

The old display task produced frames and the old driver task consumed frames
through a double or triple buffer. This separated rendering from LED I/O, but it
also created phase and backpressure problems because both tasks ran at related
fixed cadences. It added frame copies, inbox state, critical sections, and
warning paths that did not represent useful work.

Replacement: start with one LED display task per GPU node that services
requests, renders, composes, converts, and calls the platform `show()` once per
frame. Add a second queue only if hardware measurements prove it is needed.

### Layer Decay

Layer decay was a useful visual primitive. However, it ran over full buffers and
was mixed into the generic engine step.

Replacement: keep decay as a layer property, but apply it only over LEDs owned
by the current node. Consider disabling decay on layers where all animations
fully redraw each frame.

### Named Regions

`region(std::string_view)` was a nice discovery concept but inefficient and
under-specified. It only returned flattened ring/spoke arenas for the umbrella
topology.

Replacement: expose named features as typed accessors or constexpr feature
tags. Avoid string lookup in render paths.

### Event Delivery To Individual Animations

`onEvent(eventId, payload)` let `AnimWheelIndicator` receive gyro angle updates.
The idea is useful, especially for beat, FFT, button, and wheel events.

Replacement: define typed pipeline events or state slots. Avoid raw `void *`
payloads and numeric event IDs. Events should be copied into fixed latest-value
or queue slots from PubSub subscribers, then consumed by the render task.

### WebSocket LED Driver

The websocket driver was useful for visual debugging. It should not be part of
the GPU hot path, and the old implementation is tied to unrelated WiFi,
webserver, and nanopb code.

Replacement: discard it for GPU nodes. A later visualization path should live
on the master or a host-side tool. If GPU-rendered LED frames need to be fed
back to the master for visualization, treat that as optional telemetry and keep
it out of the 100 FPS render path unless measurements prove it is safe.

## Bad Concepts Or Superfluous Pieces

These should not be ported unless a later requirement explicitly reintroduces
them.

- Heap allocation in normal animation submission or frame rendering.
- `std::function` factories for animation creation.
- `std::string` names in animation objects and layer names on-device.
- `std::unordered_map` for layer lookup and pending handle publication.
- Direct FastLED includes throughout pipeline headers.
- Direct topology coupling inside the driver base.
- Hard-coded diagnostic assumptions such as `AnimWhiteDots` expecting exactly
  32 LEDs.
- Empty placeholders such as `AnimSparkle` with no behavior.
- Browser websocket mirroring as part of the production display path.
- User-facing web interfaces on GPU nodes.
- Runtime polymorphic `ILedDriver` lists for a fixed GPU node with one LED
  output platform.
- Full-frame PubSub or SPI frame distribution for LEDs. The current repo docs
  already establish that GPU nodes should receive compact events and render
  locally.

## High-Impact Risks Found

The following legacy issues are important to avoid in the port:

- The old umbrella topology dynamically allocated its spans even though the
  counts are compile-time constants.
- The old display component allocated its frame buffers on `begin()`.
- The old driver allocated inbox buffers on `setFacts()`.
- The old controller allocated every request and every animation.
- The old active-animation vector invalidated handles when entries were erased.
- The old `getAnimation<T>()` used `static_cast<T *>` rather than checked
  downcasting. A wrong type request could silently become undefined behavior.
- The old render path used floating-point `powf()` in per-frame trail loops.
  Trail scales should be precomputed or implemented with integer/fixed-point
  math.
- The old driver copied HSV to RGB, then copied RGB again into the FastLED
  buffer before `show()`.
- The old display and driver tasks communicated through a frame queue even
  though the final GPU node can own rendering and presentation in one task.
- The old FastLED pins and topology were hard-coded together, which makes the
  upcoming per-node LED group split unsafe unless the mapping is explicit.

## What To Preserve First

The first port should preserve:

- umbrella physical index sequence
- rings and spokes topology views
- fixed layer names and their intended priority order
- `MaxV`, `AddV`, and `Alpha` blend semantics
- request queue decoupling
- persistent, duration, and one-shot lifetimes
- the concept of a persistent background layer, but not the exact
  `AnimTapestry` visual
- `AnimWheelIndicator`, if a replacement event/state input is defined
- diagnostic ring, spoke, strip, endpoint, and index animations, but only after
  removing hard-coded stale assumptions

The old visually intended animations should be treated as references for
pipeline shape only, not as visual requirements. Ring, spoke, strip, endpoint,
and index diagnostics are the important animation behavior to preserve first.

The first port should not preserve the exact legacy ownership or task model.
