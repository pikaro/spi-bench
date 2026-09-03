# Sprite animation implementation plan

This document plans procedural sprite-style animations for the radial LED
surface. The target visuals are:

- a configurable star
- a growing heart
- a smiley face with facial features that can intentionally overwrite content

The plan assumes these are vector/procedural sprites, not bitmap frame
animations. That is the better fit for the current 16-spoke by 46-ring surface
and for the expected future increase in spoke count.

## Baseline constraints

- GPU rendering runs at 125 Hz with an 8 ms present-and-render budget.
- The current logical surface is 16 spokes by 46 rings.
- The physical surface is an annulus. Production strips start at roughly 60 mm
  radius and run about 417 mm, so logical radial index `0`
  is the inner visible ring, not the center of a circle.
- Each GPU renders only physically owned pixels, but animations can address
  logical pixels through `Canvas`; unowned pixels are ignored by the current
  logical-to-local map.
- Animation commands are fixed-size queue-copyable payloads. Sprite configs
  must stay small and trivially copyable.
- Current drawing primitives are limited to pixel, ring, spoke, and radial
  line helpers in `include/LedDisplay/Primitives/Canvas.hpp`.
- Current `HsvColor` has no alpha or coverage channel. `value == 0` is treated
  as transparent by the compositor, so true opaque black needs an explicit mask
  or a separate overlay path.
- Existing animations should keep rendering through supplied canvases and
  remain host-renderable through `tools/led-render/`.

## Assumptions

- These animations are normal `LedDisplay` animations under
  `include/LedDisplay/Animations/<Name>/`.
- They should use the existing layer stack unless a small masked overlay path
  is required for opaque black.
- Star and heart can ship before opaque black exists.
- Smiley should wait until black overwrite semantics are explicit; faking black
  with very-low-value HSV is not acceptable because output value floors and
  normal blend rules can make it visible or transparent in the wrong places.
- No runtime SVG/parser/formula system is needed.
- No persistent PlatformIO environment or new dependency is needed.

## Implementation strategy

Build a small shape toolkit first, then add animations on top of it.

Suggested primitive headers:

- `include/LedDisplay/Primitives/FieldCoordinates.hpp`
- `include/LedDisplay/Primitives/ShapeRaster.hpp`
- `include/LedDisplay/Primitives/SpriteDraw.hpp`

`FieldCoordinates.hpp` should align with the advanced animation pipeline plan:
compact polar and Cartesian fixed-point coordinates for logical LED centers.
The first version only needs LED center coordinates, angle wrapping, rotation,
annular radius mapping, and cheap fixed-point helpers.

`ShapeRaster.hpp` should be independent of colors and layers. It should emit
logical pixel hits through callbacks:

```cpp
struct ShapeHit {
    uint8_t spoke = 0;
    uint8_t radial = 0;
    uint8_t coverage = 0; // 0..255
};

template <typename Callback>
void lineBetween(LogicalPoint a, LogicalPoint b, uint8_t width, Callback cb);
```

The callback shape keeps the primitive useful for normal color drawing,
coverage masks, offline tests, and later geometry tools without coupling it to
`Canvas`.

For the current annular polar surface, prefer a distance-to-segment
implementation over plain grid Bresenham:

- Convert the two LED endpoints to normalized Cartesian fixed-point centers
  using physical annular radius, not strip-local radial index alone.
- Iterate logical pixels.
- Project each pixel center onto the segment.
- Emit full hits inside the requested width.
- Optionally emit a fractional neighboring hit, for example 50 percent, when
  the segment passes halfway through an adjacent LED. This is only to avoid
  obviously broken long straight lines; it is not a broad smoothing system.

This draws a visual line between two LEDs in the actual abstract canvas rather
than in a rectangular spoke/ring index grid. It also improves automatically as
more spokes are added.

`SpriteDraw.hpp` can contain color-facing helpers:

- apply hit coverage to an `HsvColor`
- draw width-controlled lines and polylines into a `Canvas`
- draw circles or circular outlines from field coordinates
- draw filled/outlined implicit shapes
- optionally mark coverage in a sprite mask once opaque sprites exist

Keep these helpers fixed-size and allocation-free.

## Rejected approaches

- Bitmap sprite frames: too much storage, awkward rotation/scaling, poor future
  spoke-count behavior.
- A general vector graphics renderer: too broad for star, heart, and smiley.
- A runtime expression or shader language: unnecessary control surface and
  unpredictable performance.
- Per-pixel trigonometry in render loops: avoid unless measured safe. Use small
  lookup tables, precomputed points, or fixed-point transforms.
- Encoding opaque black through special HSV sentinel values: fragile and likely
  to break output-floor and blend behavior.

## Phase 1: shape coordinate and line primitives

Add the minimal geometry needed by all three sprites.

Implementation steps:

1. Add `FieldPoint` or reuse the field point from the advanced animation plan:

   ```cpp
   struct FieldPoint {
       uint8_t spoke;
       uint8_t radial;
       uint16_t theta; // 0..65535 turn
       uint16_t stripRadius; // 0..65535 along the visible strip
       uint16_t worldRadius; // 0..65535 from geometric center to outer edge
       int16_t x; // signed normalized Cartesian coordinate
       int16_t y;
   };
   ```

2. Add static geometry inputs for inner radius and strip length. These values
   belong to the selected topology so legacy and production hardware retain
   their own physical coordinate models.
3. Add `fieldPoint(spoke, radial)`.
4. Add `forEachLogicalPoint(callback)` if it reduces repeated boilerplate.
5. Add `rotate(point, angle)` as a cheap fixed-point transform.
6. Add `lineBetween(LogicalPoint a, LogicalPoint b, width, callback)`.
7. Add `segmentBetween(FieldPoint a, FieldPoint b, width, callback)` if the
   line helper is cleaner in field coordinates.
8. Add unit-like host checks through the LED render tooling or a small
   compile-only test path if the project gains tests.

Acceptance criteria:

- A line between two LED centers emits both endpoints and intermediate LEDs.
- Fractional coverage is deterministic and bounded to 0..255.
- A line near the angle seam can choose the shortest angular path or an
  explicit direction. The first star defaults should avoid ambiguous seam
  crossings.
- Coordinate helpers produce an annulus: with current approximate geometry,
  the inner ring is around half the outer radius, not zero.
- Existing animations compile unchanged.

## Phase 2: star animation

The star is the first sprite because it is radial, cheap, and useful for
validating the line primitive.

Suggested files:

- `include/LedDisplay/Animations/Star/Config.hpp`
- `include/LedDisplay/Animations/Star/Animation.hpp`
- `include/LedDisplay/Animations/Star/Command.hpp`
- `include/LedDisplay/Animations/Star/CommandDesc.hpp`

Suggested config:

```cpp
struct WIRE_MSG StarConfig {
    uint8_t hue = 32;
    uint8_t saturation = 255;
    uint8_t value = 220;       // innermost/valley value
    uint8_t tipCount = 4;      // 4 or 8 on the current 16-spoke surface
    int8_t tipSlopePct = 40;   // signed brightness delta from valley to tip
    uint8_t strokeWidth = 2;
    bool outerOrigin = true;
};
```

`tipSlopePct` should be signed. Positive values make tips and adjacent slope
spokes brighter than the inner valley; negative values make them dimmer. Clamp
the computed value to 0..255.

Shape model:

- A tip is placed on the outer ring.
- The inner valley between tips is placed against the visible strip, not at the
  geometric center. A first version can use a fixed strip-local fraction, for
  example 10-25 percent of strip depth from the inner visible ring.
- Each tip connects to the neighboring valleys through line segments.
- For 16 spokes, allow 4 and 8 tips by default.
- For future spoke counts, accept a `tipCount` only when the valley and slope
  spokes can be represented cleanly. Reject or clamp unsupported counts in
  command parsing rather than producing misleading shapes.

Animation model:

- At `elapsed == 0`, draw only the outer tip points.
- As progress advances, draw each edge from the tip toward its adjacent valley.
- The next spoke starts lighting naturally when the line reaches it. Fractional
  neighboring hits are only needed if the line visibly skips LEDs.
- The animation can rely on the `Effect` layer decay for wake unless a fully
  deterministic trail is required later.

Default layer:

- `Effect`

Render extent:

- Owned-pixel rendering through `Canvas` is enough. No full-frame scratch is
  required because every GPU can evaluate the same logical shape and only emit
  its owned pixels.

Command:

- `/anim star [durationMs] [hue] [value] [tipCount] [tipSlopePct] [strokeWidth]`

Validation:

- Render 4-tip and 8-tip examples in the radial viewer.
- Confirm that 8-tip output does not become an every-other-spoke flicker with
  no readable star body.
- Confirm default `tipSlopePct` is readable but not overexposed.

## Phase 3: heart animation

The heart should be a field shape that scales and rotates over time. It should
not be a bitmap and should not need many parameters.

Suggested files:

- `include/LedDisplay/Animations/Heart/Config.hpp`
- `include/LedDisplay/Animations/Heart/Animation.hpp`
- `include/LedDisplay/Animations/Heart/Command.hpp`
- `include/LedDisplay/Animations/Heart/CommandDesc.hpp`

Suggested config:

```cpp
struct WIRE_MSG HeartConfig {
    uint8_t hue = 224;
    uint8_t saturation = 255;
    uint8_t value = 220;
    uint8_t rotation = 0; // Angle<uint8_t>-style turn
    bool filled = true;
};
```

Implementation options:

- Implicit field equation:
  `(x*x + y*y - 1)^3 - x*x*y*y*y <= 0`
- Sampled parametric outline:
  `x = 16 sin^3(t)`,
  `y = 13 cos(t) - 5 cos(2t) - 2 cos(3t) - cos(4t)`
- Hybrid:
  use the implicit equation for filled coverage and a sampled polyline for a
  cleaner outline.

Preferred first implementation:

- Use the implicit field equation in fixed-point math for fill/inside tests.
- Use a narrow threshold around the implicit boundary for outline mode.
- Add a sampled polyline only if the outline looks poor in the host renderer.

Annular growth model:

- The heart is evaluated in geometric center coordinates, but LEDs only exist
  in the annulus. Do not spend a large part of the animation invisible inside
  the center gap.
- Start at a scale where the shape is just reaching the inner visible ring.
- Scale up over `durationMs` until the heart boundary has moved past the outer
  display edge.
- Apply `rotation` as an inverse transform before evaluating the shape.
- Apply a short fade-out near the end of lifetime.

The fade-out matters for `filled = true`: a filled shape that only scales larger
would eventually cover the entire canvas and remain visible until it is removed.
For outline mode, the outline naturally leaves the canvas; for filled mode, the
last part of the animation should fade as the shape exits.

Default layer:

- `Effect`

Render extent:

- Owned-pixel rendering through `Canvas` is enough.

Command:

- `/anim heart [durationMs] [hue] [value] [filled] [rotation]`

Validation:

- Render at rotations 0, 64, 128, and 192.
- Verify the top cleft and lower point are readable at 16 spokes.
- Verify the same implementation improves with more virtual spokes in the host
  renderer when that topology becomes available.

## Phase 4: opaque sprite coverage

This phase is required before smiley if facial features need real black.

Current problem:

- The compositor skips source pixels whose HSV value is zero.
- A black feature drawn into scratch becomes transparent during layer blend.
- If a layer stores only `HsvColor`, it cannot distinguish "untouched" from
  "covered with black".

Preferred solution:

- Add an explicit coverage/opacity mask for sprite drawing.
- Keep the mask optional and local to animations or layers that need it.
- Compose covered pixels even when `HsvColor::value == 0`.

Possible implementation shapes:

1. Masked sprite scratch:
   - Add one reusable `uint8_t` coverage buffer parallel to scratch.
   - A masked sprite renders color plus coverage.
   - The engine composites covered scratch pixels into the target layer.
   - This is enough only if the target layer can preserve black coverage during
     final layer composition.

2. Masked overlay layer:
   - Add a small final overlay path with color plus coverage.
   - Opaque sprites compose after normal layers directly into the output frame.
   - This avoids changing every existing layer buffer.
   - It is less general, but it matches the smiley requirement closely.

3. General masked layers:
   - Store color plus coverage for selected layers.
   - Update `LayerStack` and `Compositor` to blend masked layers normally.
   - This is the most general option but touches more shared rendering code.

Recommended first choice:

- Use a final masked overlay path only for animations that explicitly require
  opaque coverage.
- Keep ordinary color-only animations on the existing scratch/layer path.
- Add a trait such as `static constexpr bool usesOpaqueCoverage = true` only
  when needed.

Memory estimate:

- Current local pixel count is 368 per GPU.
- One `uint8_t` coverage buffer costs about 368 bytes per GPU on the owned
  path, or 736 bytes for full logical coverage.
- This is acceptable for a gated feature but should not be added to every layer
  by default without a measured need.

Acceptance criteria:

- A covered black pixel overwrites background, FFT, and effect content.
- Uncovered pixels remain transparent.
- Existing `value == 0` behavior for normal layers is unchanged.
- The host renderer can represent opaque black in traces or at least in viewer
  output.

## Phase 5: smiley animation

The smiley uses the same growth/rotation idea as the heart, but its facial
features must stay extremely coarse. The current resolution is tiny, and even a
future 4x spoke increase should not assume detailed facial geometry.

Suggested files:

- `include/LedDisplay/Animations/Smiley/Config.hpp`
- `include/LedDisplay/Animations/Smiley/Animation.hpp`
- `include/LedDisplay/Animations/Smiley/Command.hpp`
- `include/LedDisplay/Animations/Smiley/CommandDesc.hpp`

Suggested config:

```cpp
struct WIRE_MSG SmileyConfig {
    uint8_t hue = 40;
    uint8_t saturation = 255;
    uint8_t value = 220;
    uint8_t rotation = 0;
    bool filled = true;
};
```

Shape model:

- Face: filled circle or circular outline in annular field space.
- Eyes: two single covered black pixels selected from fixed face-relative
  positions.
- Mouth: four straight line segments forming a shallow smile. Do not implement
  an arc or curve for the first version.

Feature defaults should be fixed in code at first:

- choose the nearest logical LED for each eye after scale and rotation
- use a five-point mouth polyline, giving four segments
- keep the mouth stroke width around two LEDs unless host traces show that it
  closes up the face
- use fractional neighboring hits only where a straight segment visibly misses
  an adjacent LED it should graze

Rendering behavior:

- For `filled = true`, draw the face as color, then draw eyes and mouth as
  opaque black coverage.
- For `filled = false`, draw the face outline and facial features as colored
  strokes unless opaque black still reads better in testing.
- Apply rotation to the face coordinate frame so rotated smileys can be spawned
  in a sequence.

Default layer:

- Use the masked overlay path if implemented.
- Otherwise use `Effect`, but only for non-black colored outline tests.

Command:

- `/anim smiley [durationMs] [hue] [value] [filled] [rotation]`

Validation:

- Confirm the eyes are two readable black pixels at 16 spokes.
- Confirm the four-segment mouth reads as a smile, not a V and not a random
  broken line.
- Confirm black features overwrite active FFT/effect content.
- Confirm the overlay path does not erase unrelated pixels outside the sprite.

## Command and registry work

For each animation:

1. Add a new `AnimationKind`.
2. Add config, animation, command, and command description files.
3. Add the animation to `Animations/All.hpp`.
4. Add dispatch and style entries in `Animations/Registry.hpp`.
5. Add generated host-registry support if the current generator needs updates.
6. Add `/anim` command help text with non-obvious arguments documented.

Backwards compatibility is not required for new commands, but existing commands
should remain unchanged.

## Master orchestration

Do not wire the new sprites into automatic show orchestration in the first
commit that adds them.

First expose manual commands and host-render examples. After the shapes are
visually validated:

- star can be used as a beat or bell accent
- heart can spawn in a sequence with varied rotations
- smiley can be a rare overlay event, especially if it intentionally blacks out
  facial features over other layers

Keep orchestration policy in `src/master/orchestration.hpp`, not inside the
animations.

## Host render tooling

Tooling support is part of the work, not optional.

Add examples:

- `tools/led-render/examples/star-4.json`
- `tools/led-render/examples/star-8.json`
- `tools/led-render/examples/heart-filled.json`
- `tools/led-render/examples/heart-outline-rotations.json`
- `tools/led-render/examples/smiley-filled.json`

Useful local validation:

```sh
bin/led-render --config tools/led-render/examples/star-4.json \
  --output /tmp/star-4.tled
bin/led-view /tmp/star-4.tled --layout radial --glare
```

For opaque black, the viewer should display covered black as black, not as
transparent. If the trace format cannot express coverage yet, update it in the
same phase as masked overlay support.

## Build and hardware validation

Build at least the command publisher and GPU consumers:

```sh
bin/build -e master
bin/build -e gpu0
bin/build -e gpu1
```

Build all active environments if any shared PubSub, wire, generated-code, or
global config surface changes:

```sh
bin/build -e master
bin/build -e media
bin/build -e gpu0
bin/build -e gpu1
bin/build -e io
```

Hardware checks:

```text
!master /anim star
!master /anim heart
!master /anim smiley
!gpu0 /metrics
!gpu1 /metrics
```

Acceptance criteria:

- no command decode failures
- no render failures
- no missed strobes
- `ledDisp.slow == 0`
- `stepMax` remains below 8000 us with useful headroom
- host radial traces and hardware both show recognizable shapes

## Decision gates

Gate 1: line and field primitives.

- Existing animations still compile and render.
- Line coverage looks stable in host traces.
- No layer or compositor behavior changes yet.

Gate 2: star.

- 4-tip star is clearly readable.
- 8-tip star is acceptable on the current 16 spokes.
- The slope parameter visibly changes tip/valley emphasis without clipping by
  default.

Gate 3: heart.

- Filled and outline hearts are recognizable.
- Rotation is useful enough to justify keeping the parameter.
- Filled growth fades out cleanly instead of covering the whole canvas until
  expiration.

Gate 4: opaque coverage.

- Black can be represented explicitly and host-rendered.
- Existing transparent zero-value behavior is unchanged outside masked sprites.

Gate 5: smiley.

- Face, black-pixel eyes, and the four-segment mouth read at 16 spokes.
- Opaque black features suppress lower layers only where covered.
- The implementation remains small enough that more sprites could reuse the
  same primitives without broad renderer changes.

## Summary

The safe path is a small procedural shape toolkit followed by star, heart, then
smiley. Star validates line growth and spoke-aware brightness with little engine
risk. Heart validates field-space scaling and rotation. Smiley should come last
because true black facial features require explicit coverage semantics that the
current HSV-only compositor does not have.
