# Per-Pixel Alpha Rendering Analysis

Status: **analysis revised from concrete visual requirements; final consistency review in progress**  
Date: 2026-08-27

> **Revision note (2026-08-27):** The initial recommendation in this record was
> a UI-only alpha path. The owner rejected that scope: UI is only an indicator;
> animations are the valuable output. That recommendation is withdrawn. The
> retained findings about RAM, HSV compositing, output conversion, and UI
> lifetime remain evidence, but the target architecture must make per-pixel
> alpha available to animation authors.
>
> **Geometry note (2026-08-27):** The production topology changed from 72 to
> 60 LEDs per spoke after the resource calculations in this analysis. The
> 32×72 figures below are a conservative historical baseline. Refresh the
> exact storage/build numbers against the 32×60 image before implementation;
> the compositing conclusions are unchanged.
>
> **Second revision note (2026-08-27):** An intermediate corrected pass proposed
> separate RGB composition lanes above the legacy scene. The owner then pointed
> out that the engine already renders animations sequentially through scratch
> into one retained layer. No animation-private surfaces or parallel lanes are
> needed for same-layer interaction: the retained layer only needs to store the
> RGB result of each sequential blend. The selected core model is now HSV+alpha
> animation scratch → premultiplied-RGBA retained layer → opaque RGB
> presentation.

## Question

Evaluate the cost and value of adding per-pixel alpha to the GPU rendering
pipeline now that a retained topmost `UI` layer exists. The analysis covers:

- RAM, static-object, memory-bandwidth, and CPU implications at the production
  32×72 geometry and 125 fps;
- the semantic contract required for alpha in the current HSV pipeline;
- benefits to UI elements and existing animations;
- genuinely new effect families enabled by alpha;
- narrower alternatives if full pipeline-wide alpha is not justified.

No implementation is part of this analysis.

## Corrected working verdict

The issue is not that HSV was a strange choice. HSV is an excellent animation
**authoring and intensity** space: hue movement, saturation, brightness
envelopes, palette fields, and the current `MaxValue`/`AddValue` effects are
compact and intuitive. It is a poor **translucent composition** space because
source-over alpha describes how emitted red, green, and blue contributions mix;
hue is circular and HSV saturation/value are nonlinear coordinates of those
channels.

Therefore the corrected target is **HSV animation authoring with an explicit
coverage/alpha channel, followed by RGB composition at a deliberately chosen
boundary**. Alpha must not be confined to UI. It also must not silently redefine
all current low-value pixels as translucent: existing animations use value as
emitted intensity and must remain visually exact when they do not opt into
alpha.

RAM still does not explode. The concrete examples resolve the conversion
boundary: HSV animation results become RGB when scratch is blended into a
retained layer. Later animations blend onto that retained
premultiplied-RGBA result without knowing which animations produced it. Final
layer composition stays in RGB and presentation is opaque RGB. This avoids
every RGB→HSV round-trip.

The remaining decisions are compatibility/tuning questions rather than alpha
feasibility questions:

1. How should legacy `MaxValue`, HSV hue-winning `AddValue`, and subtractive HSV
   value decay be preserved or adapted after a retained layer has accepted a
   true RGB mixture? No-alpha output must remain an exact regression contract.
2. When output is final RGB and the optional value floor is enabled, should it
   apply to legacy HSV contributions before RGB alpha composition or acquire a
   defined RGB equivalent on the completed pixel? A zero configuration must
   continue to turn it off.

The continued analysis below preserves rejected alternatives as research
history and develops the selected sequential retained-layer model. No
implementation should start from the withdrawn UI-only or separate-lane
recommendations.

## Verified current pipeline facts

- `HsvColor` is three bytes: hue, saturation, and value.
- Each GPU owns 1,152 pixels while animations address a 2,304-pixel logical
  surface.
- The layer stack currently owns eight 1,152-pixel layer frames and one shared
  1,152-pixel scratch frame. The engine additionally owns one 2,304-pixel
  full-frame scratch buffer, and triple presentation owns three more 1,152-pixel
  frames.
- Animations render HSV into shared scratch. Scratch is blended into a retained
  layer using the animation's uniform `AnimationStyle`; layers are then blended
  into an HSV presentation frame using each layer's uniform style.
- The existing `BlendOp::Alpha` is not conventional per-pixel source-over
  alpha. It applies the uniform style opacity to source `value`, retains a
  correspondingly scaled destination `value`, adds the two values, and chooses
  hue/saturation from whichever scaled value is larger.
- `Canvas` primitives accept `HsvColor` plus a blend operation. They have no
  coverage or opacity argument independent of HSV value.
- The `UI` layer is composed last with `Replace` at full layer opacity. Its
  nonzero pixels therefore replace lower layers. It is retained, cleared when
  a new UI animation starts, and decays `value` by one per frame after the
  short-lived animation stops.
- The first UI animation, `RadialGauge`, writes opaque configurable ring-band
  pixels with `Replace`; it currently needs only binary coverage.
- SK9822 output accepts only final RGB plus the common five-bit hardware
  brightness. Per-pixel alpha is an internal rendering/composition concern and
  would not change the 4,760-byte SPI frame.

### Code evidence map

| Concern | Current source |
| --- | --- |
| Three-byte HSV/RGB types | [`Color.hpp`](../include/LedDisplay/Interfaces/Color.hpp) |
| Primitive write/sample contract | [`Canvas.hpp`](../include/LedDisplay/Primitives/Canvas.hpp) |
| Uniform opacity and HSV blend behavior | [`Compositor.hpp`](../include/LedDisplay/detail/Compositor.hpp) |
| Eight retained layers, UI policy, shared scratch | [`LayerStack.hpp`](../include/LedDisplay/detail/LayerStack.hpp) |
| Command drain, animation render, final compose | [`AnimationEngine.hpp`](../include/LedDisplay/detail/AnimationEngine.hpp) |
| Triple-frame snapshot ownership | [`PresentBuffers.hpp`](../include/LedDisplay/detail/PresentBuffers.hpp) |
| Present-before-render sequencing and metrics | [`Display.hpp`](../include/LedDisplay/detail/Display.hpp) |
| SK9822 HSV conversion, floors, brightness, framing | [`Sk9822Encoder.hpp`](../include/LedDisplay/Outputs/detail/Sk9822Encoder.hpp) |
| Legacy FastLED output policy | [`FastLedOutput.hpp`](../include/LedDisplay/Outputs/FastLedOutput.hpp) |
| Current opaque UI producer | [`RadialGauge/Animation.hpp`](../include/LedDisplay/Animations/RadialGauge/Animation.hpp) |
| Existing heuristic-alpha overlay | [`WheelIndicator/Animation.hpp`](../include/LedDisplay/Animations/WheelIndicator/Animation.hpp) |
| Fixed-width trace schema/writer | [`TraceFormat.hpp`](../tools/led-render/TraceFormat.hpp), [`host_render.cpp`](../tools/led-render/host_render.cpp) |
| Three-channel Python trace reader | [`trace.py`](../tools/led_render_py/led_render/trace.py) |

## Required semantic contract

The implementation must not treat HSV `value` and alpha as interchangeable:

- `value` is emitted color intensity. Lowering it produces a dimmer source.
- alpha is coverage/transmittance. Lowering it reveals more of the destination.

They look similar only over black. A dim white `Replace` pixel over a bright
colored animation darkens and hides that animation; translucent white should
mix with it into a pastel. This distinction is exactly what element-local UI
fades need.

Three independent opacity sources may exist and should be named separately:

1. per-pixel coverage/alpha produced by a primitive or animation;
2. uniform animation opacity in `AnimationStyle`;
3. uniform retained-layer opacity in `LayerConfig`.

For source-over, their product is the effective source alpha. The contract also
has to state at which blend each factor is consumed. In particular, a retained
UI element should preserve its authored per-pixel alpha; consuming it while
writing scratch into the layer loses the information needed for the later
layer-to-scene blend.

The shared UI expiry fade should be a separate uniform lifetime/layer factor,
not `alpha -= 1` on stored pixels. Subtractive alpha decay would make an
antialiased edge authored at alpha 32 disappear after 0.256 seconds while an
opaque center lasts 2.04 seconds. Scaling every authored alpha by one common
255→0 fade preserves the element's shape and relative edge coverage. If the UI
is stored premultiplied, the same factor must scale both premultiplied color and
alpha at composition; stored pixels need not be destructively modified.

`Replace` and source-over also need separate meanings at separate boundaries.
Today one `BlendOp` type is reused for primitive-to-scratch,
scratch-to-retained-layer, and retained-layer-to-presentation operations. That
is manageable while pixels contain only color, but becomes ambiguous with
coverage: “replace this stored UI pixel” is not the same operation as “compose
this translucent stored UI pixel over the completed scene.” Likewise,
`MaxValue` and `AddValue` need explicit alpha-propagation rules if alpha is made
global. Guessing those rules would change existing visuals.

The safety contract should therefore be:

- all legacy HSV animation and layer paths remain exact when alpha is unused;
- alpha is opt-in, not inferred from nonzero `value` globally;
- transparent and opaque black remain representable as distinct states in an
  alpha-bearing surface;
- alpha is discarded before hardware output; it has no SPI meaning;
- no HSV hue or saturation interpolation is presented as true source-over.

## Color-space boundary

Correct source-over is channel-wise color arithmetic. It is well defined for
RGB but not for the current HSV representation: hue wraps at 255, saturation
is nonlinear with respect to coverage, and hue is meaningless near black or
gray. The present compositor's winner-takes-hue/saturation rule is a useful
emissive heuristic, not translucent color mixing.

The least disruptive place for true alpha is consequently **after the legacy
HSV layers have composed, at the HSV-to-RGB output boundary**:

1. compose all existing animation layers to opaque final HSV exactly as now;
2. convert each final HSV pixel to RGB once, using the selected existing
   conversion;
3. source-over the alpha-bearing UI pixel in RGB;
4. present/encode the resulting opaque RGB and discard alpha.

This preserves the current effects' blend behavior and still provides correct
UI translucency. Blending in the selected output RGB space also preserves the
project's current conversion character. Introducing a separate gamma or
linear-light policy at the same time would be an independent visual change and
is outside this feature.

Converting the result back to HSV would add cost and lose color fidelity. A
design that requires an RGB-to-HSV round-trip should be rejected.

## Representation and RAM bounds

The straightforward representation is a four-byte HSV-plus-alpha pixel. At the
current production geometry, changing every render/present pixel from three to
four bytes has this exact static-storage delta per GPU:

| Storage owner | Pixels stored | Current HSV bytes | Four-byte pixel bytes | Delta |
| --- | ---: | ---: | ---: | ---: |
| Eight retained layer frames | 9,216 | 27,648 | 36,864 | +9,216 |
| Shared animation scratch | 1,152 | 3,456 | 4,608 | +1,152 |
| Full-logical scratch | 2,304 | 6,912 | 9,216 | +2,304 |
| Triple present buffers | 3,456 | 10,368 | 13,824 | +3,456 |
| **Total** | **16,128** | **48,384** | **64,512** | **+16,128** |

This is the deliberately pessimistic “alpha everywhere, including after final
composition” model. Final hardware output has no alpha, so a design that keeps
the presentation buffers opaque avoids the last 3,456-byte delta and adds
12,672 bytes instead.

A UI-only design can be much smaller. Replacing the existing retained
three-byte UI pixels with four-byte UI pixels adds 1,152 bytes. Reusing the
existing shared HSV scratch for UI color and adding a 1,152-byte scratch alpha
plane adds another 1,152 bytes, for a 2,304-byte total delta. The UI result can
be converted to retained RGB(A) when scratch is accepted, so final composition
does not repeatedly convert its color. A completely separate four-byte UI
scratch, while retaining the legacy shared HSV scratch, instead makes the total
delta 5,760 bytes. Both fit comfortably.

If UI coverage is encoded in its existing HSV `value` and given specialized
composition semantics, the RAM delta can be zero, at the cost of making color
intensity and coverage inseparable. That is not the recommended true-alpha
contract.

The SK9822 DMA frame, logical maps, animation slots, task stacks, PubSub
payloads, and physical wire bandwidth do not grow. Alpha is consumed before
the ordinary RGB SK9822 frame is encoded.

### Measured current-image context

A current `gpu0` build including the eighth/UI layer links at 208,628 / 327,680
bytes of reported RAM (63.7%) and 1,135,719 / 2,097,152 bytes of flash. Debug
type layout reports:

- `Display`: 74,576 bytes;
- `LayerStack`: 31,152 bytes, including 27,696 bytes for eight layer states;
- `AnimationEngine`: 50,012 bytes;
- triple `PresentBuffers`: 10,384 bytes;
- `SelectedOutput`: 4,824 bytes.

Against that linked baseline:

- pessimistic four-byte pixels everywhere project to 224,756 bytes (68.6%),
  leaving 102,924 bytes of reported RAM;
- omitting alpha from opaque present buffers projects to 221,300 bytes (67.5%);
- a 2,304-byte UI-only retained/scratch alpha provision projects to 210,932
  bytes (64.4%);
- the more isolated 5,760-byte UI retained/scratch design projects to 214,388
  bytes (65.4%), leaving 113,292 bytes.

These are exact static-array deltas applied to a measured link result, not a
link of candidate code. They exclude any implementation-dependent alignment or
new control structures and do not replace an on-device heap-low-water check.
Even the pessimistic option does not constitute a RAM explosion on the current
image.

### Important initialization incompatibility

Adding `alpha` directly to `HsvColor` creates a non-obvious source-compatibility
trap:

- Existing designated initializers omit alpha and must continue to mean opaque
  colored pixels.
- Existing `HsvColor{}` and frame clearing mean “black/absent” and would need to
  mean transparent in a true-alpha model.
- Giving alpha a default of 255 preserves color literals but makes default-cleared
  pixels opaque black; giving it a default of zero preserves clearing but makes
  every existing color literal invisible.

This strongly favors a distinct alpha-bearing pixel or explicit clear/color
factories over casually appending a byte to `HsvColor`.

## Architecture options

### 1. Four-byte HSVA throughout the existing pipeline

This is superficially simple and has acceptable RAM cost, but is not a complete
solution. It has the initialization incompatibility above, forces alpha rules
onto `Replace`, `MaxValue`, and `AddValue`, and still cannot perform correct
source-over in HSV. It also carries alpha through full-logical and opaque
presentation buffers that do not inherently need it.

**Assessment:** not recommended. Its main cost is semantic and regression risk,
not RAM.

### 2. Treat per-pixel alpha as another HSV value scale

Coverage can scale source `value` before the existing blend. This costs one
small integer scale for participating pixels and can require no extra buffer if
the existing value byte doubles as coverage.

This is useful for emissive masks and soft edges over black or with `MaxValue`,
but it is not translucency. It cannot make a white UI overlay reveal a colored
scene correctly, and it cannot represent independently dim-and-translucent
elements.

**Assessment:** a valid narrow effect primitive, but it does not answer the UI
question and should not be called alpha.

### 3. Convert every retained layer to RGB(A)

True source-over becomes straightforward, particularly with premultiplied RGB.
However, animations currently generate HSV and depend on the existing
`MaxValue`/`AddValue` HSV semantics. Converting every contributing layer—or
every animation scratch result—to RGB moves HSV conversion from once per final
pixel to potentially once per active layer/animation pixel. Recreating the old
blend operations in RGB would also alter their appearance.

**Assessment:** technically general, but the highest CPU and visual-regression
risk. There is no demonstrated requirement that justifies converting the
existing effects pipeline.

### 4. Dedicated alpha-bearing UI surface, composed after legacy HSV layers

Keep all existing layers and animation rendering in HSV. Store UI color plus
alpha separately, compose the non-UI scene exactly as now, convert the opaque
scene once to RGB, then source-over UI in RGB. A straight RGBA UI surface is a
good first representation because element alpha can decay independently from
its stored color; with only one topmost UI surface, the extra per-channel source
multiplication is immaterial. Premultiplication becomes more attractive only
if many translucent surfaces are repeatedly composited.

This design requires an explicit presentation snapshot. The current triple
buffers contain the completed HSV scene, and presentation of one buffer occurs
before rendering the next. The output stage must not read the live retained UI
surface after it has advanced to a newer frame. Two viable snapshots are:

- make triple presentation buffers contain the final opaque RGB result; or
- snapshot both base HSV and UI RGBA/alpha for each pending presentation.

Final opaque RGB buffers retain the current three bytes per pixel and avoid the
pessimistic presentation-buffer alpha cost. They do, however, widen the output
interface from HSV input to final RGB input. Both SK9822 and legacy FastLED
outputs already ultimately consume RGB, so this is an abstraction change, not
wire growth.

The UI surface's straight-versus-premultiplied representation depends on how UI
elements write it:

- straight RGBA is simplest if a new element replaces stored pixels and only
  the final UI-to-scene blend is source-over;
- premultiplied RGBA is the safer efficient representation if multiple
  translucent UI elements are themselves source-over composited into the
  retained UI surface, because repeated straight-alpha composition otherwise
  needs normalization/division.

That choice should be made with the UI element lifetime model, not hidden in a
generic color type.

**Assessment after owner review:** rejected as the target architecture. It is
cheap and correct for UI but spends structural complexity on the least valuable
consumer and does not give animation authors the requested capability. Its
presentation and color-space findings remain applicable to a general overlay.

### 5. Specialized white-UI mask or retained UI elements

The current brightness indicator is white, so a one-byte white coverage mask
could be composed cheaply. Alternatively, the engine could retain UI element
descriptions and re-render their uniform opacity each frame instead of retaining
pixels. Both reduce the immediate API surface, but the former hard-codes a
color limitation and the latter ceases to be a general retained pixel layer.
Either still needs RGB source-over for actual translucency.

**Assessment:** reasonable prototype strategies, but poor long-term contracts
if colored icons, text, or overlapping UI elements are expected.

### Withdrawn initial scope boundary

The initial pass recommended true per-pixel alpha only for an opt-in UI
render/composite path. The owner rejected that priority because animations are
the valuable product and UI is only an indicator. Do not implement this scope.

The useful retained insight is that an opt-in alpha path can preserve the legacy
HSV compositor. The corrected alternatives below apply that insight to general
animation overlays or to all animation layers rather than UI alone.

## Corrected global-animation architecture choices

### A. HSVA retained layers, RGB source-over only between completed layers

Animations keep producing HSV plus an independent coverage byte. Scratch and
retained layer frames become four-byte HSVA. Existing primitive-to-scratch and
scratch-to-layer `Replace`, `MaxValue`, and `AddValue` operations remain HSV
operations and propagate the selected/resulting coverage under explicit rules.
During final composition, each enabled layer pixel is converted to RGB and its
per-pixel alpha is source-over composited.

This makes alpha available to every animation **as a layer contribution** and
keeps HSV generation, decay, and legacy blending intact. With five enabled
layers it raises final HSV conversion from 144,000 to at most roughly 720,000
layer-pixels/second per GPU, plus RGB blend arithmetic. That remains plausible
on the S3 but must be measured.

The limitation is fundamental: two translucent animations already flattened
into the same HSVA retained layer cannot source-over one another correctly.
`Replace` and `MaxValue` can select one HSVA result; `AddValue` can retain its
emissive HSV behavior; none can represent the RGB mixture of two translucent
colors as a stable HSVA pixel without an RGB→HSV round-trip. Animations needing
independent source-over must occupy different layers.

**Assessment:** lowest-disruption way to put alpha on all animation outputs,
provided layer-level rather than arbitrary animation-level source-over is an
acceptable contract.

### B. General RGB animation-composition lanes over the legacy scene

Keep the complete current HSV layer stack exact. Add general alpha-capable
animation surfaces—not a UI-specific surface—that any animation may target.
Such an animation still authors HSV+coverage in scratch; accepting scratch
converts it to the RGB representation required by its composition operation.
The completed legacy HSV scene converts once to RGB and the new animation
surfaces compose on top. UI is merely one final consumer.

Source-over and additive light cannot in general be flattened into one ordinary
RGBA bitmap and still be applied later to an unknown background: source-over
attenuates the destination, while additive light does not. The simple robust
form is therefore a small number of explicitly ordered composition lanes:

- a light-interaction lane storing premultiplied RGB contribution for
  alpha-weighted additive waves/glows;
- a premultiplied-RGBA translucent lane for source-over animation content;
- the final premultiplied-RGBA UI lane with its separate lifetime.

Multiple animations using the same operation blend correctly within their lane.
If arbitrary interleaving of additive and source-over operations is ever
required, a richer retained per-pixel affine transform (`out = dst*T + E`) can
represent both, but six-channel transform storage and its more complex decay
contract are not justified by the current examples.

RGB conversion is proportional to pixels written by active alpha animations
rather than every enabled legacy layer. The cost of flattened retained lanes is
that old contributions cannot be removed or expired independently unless the
lane clears/re-renders or stores element/animation state. All alpha content is
also above the legacy scene; it cannot sit between the existing FFT, Effect,
Transient, Wheel, and Debug layers.

**Assessment after pipeline correction:** superseded. It is a viable migration
fallback, but separate lanes are unnecessary for same-layer animation
interaction because `blendScratch()` already serializes animation results into
the retained destination.

### C. Premultiplied-RGBA retained storage for every layer

Animations may still author HSV+coverage in a scratch frame, but scratch is
converted when blended into a retained premultiplied-RGBA layer. Source-over
within a layer and between layers is then cheap and well defined. The storage
increase is still in the same modest class as global HSVA: four-byte retained
pixels add 9,216 bytes, a scratch alpha plane adds 1,152, and full-logical alpha
adds 2,304; opaque RGB presentation remains three bytes. The approximate total
delta is 12,672 bytes before control/alignment details.

This is the cleanest general-alpha compositor and avoids repeated conversion of
retained layers. It is also the greatest threat to the current look. A retained
RGB pixel no longer contains the HSV value/hue/saturation state required to
reproduce the existing `MaxValue`, hue-winning `AddValue`, and subtractive HSV
value decay exactly after arbitrary alpha mixtures. RGB scaling/addition can be
defined, but it is a new visual contract. Keeping extra HSV proxy state makes
storage and rules more complex and still cannot assign one meaningful HSV color
to a true RGB mixture.

**Assessment after pipeline correction:** selected core direction. It is
correct for sequential same-layer source-over and likely feasible in RAM and
CPU. It is a graphics-pipeline migration rather than a small feature, so it
requires an exact no-alpha compatibility path plus side-by-side host traces and
physical visual approval wherever a layer opts into RGB mixing.

### D. One retained surface per active animation

Retaining RGBA independently for every active slot preserves arbitrary order,
updates, and lifetimes. At the configured maximum of 32 animations, full owned
RGBA surfaces alone require 147,456 bytes per GPU before existing layers,
scratch, presentation, stacks, and output storage. That exceeds the comfortable
headroom of the current image. A much smaller fixed alpha-surface pool or
re-rendered scene graph could be designed, but would introduce allocation,
admission, and lifetime policies absent from the current engine.

**Assessment:** resource use does start to explode in this model. Do not choose
it without a concrete independent-animation requirement and an explicit small
pool bound.

### Semantic distinction resolved by concrete examples

“Per-pixel alpha for animations” has two materially different meanings:

1. each animation can author coverage for how its **completed retained layer**
   overlays other layers; or
2. every active animation must source-over other animations correctly even when
   they target the same retained layer.

The owner supplied three intended results: a translucent full-white volume
indicator, color interaction between simultaneous waves, and FFT background
remaining visible through a wave wake. The owner then clarified the important
existing mechanism: animations do not need retained private frames. For each
active slot, the engine clears shared scratch, renders one animation, and calls
`blendScratch()` into the retained target layer. A later wave can blend onto the
result already stored by the earlier wave without either animation knowing
about the other.

Choice 2 is therefore required only for the retained destination of an
alpha-capable layer. Scratch can remain HSV plus alpha; the retained layer must
store premultiplied RGBA so the first wave's RGB mixture survives for the next
sequential blend. This removes the per-animation-surface cost and lifetime
complexity from the core design.

## Blend semantics derived from the examples

Per-pixel alpha is a source-contribution/coverage control. It does not imply one
universal color-combination operator.

| Desired result | Appropriate operation | Destination behavior |
| --- | --- | --- |
| White volume indicator revealing the animation | RGB source-over | Destination is attenuated by inverse alpha |
| Two colored light waves interacting | RGB source-over as the first experiment | The later wave contributes proportionally instead of replacing the earlier wave |
| FFT visible through a wave wake | Low-alpha RGB source-over | The background is attenuated only by the wake's small alpha |
| Existing brighter-source effects | Legacy HSV `MaxValue` | Exact current winner semantics |
| Existing debug/light accumulation | Legacy HSV `AddValue` | Exact current saturating-value semantics |

For an opaque destination, source-over is approximately:

```text
out = srcRgb * alpha + dstRgb * (1 - alpha)
```

For an optional emitted-light interaction, alpha-weighted additive is:

```text
out = saturatingAdd(dstRgb, srcRgb * alpha)
```

Source-over alone satisfies all three stated results and should be the first
contract. Alpha-weighted additive remains a useful later animation mode because
it leaves the destination unattenuated and makes emitted colors accumulate, but
it can drive overlaps toward white. Screen is another later tuning option. None
is required to decide the retained pixel representation.

The current `BlendOp::Alpha` must not silently become source-over: existing
animations use its HSV value-mix heuristic and would change. The contracts
should retain/rename that behavior explicitly and add clearly named RGB
operations such as `SourceOver` and `AdditiveAlpha` for the new compositor.

## Selected sequential retained-layer mechanism

The existing engine control flow is already correct for per-animation alpha:

1. Clear shared animation scratch.
2. Render one animation into HSV color plus per-pixel alpha scratch.
3. Convert its owned scratch pixels to premultiplied RGB and blend them into the
   retained target layer.
4. Repeat for the next active animation. It sees the previous result only
   through the compositor-owned retained layer.
5. Compose retained RGBA layers into an opaque RGB presentation frame.

There is no per-animation framebuffer. `blendScratch()` is the ownership and
color-space boundary. Source-over ordering is simply the engine's defined
animation render order; because slot reuse currently makes raw slot order an
unstable semantic, the eventual contract should explicitly define later-played
or explicit z-order behavior if users can observe it.

### Concrete mapping of the requested results

- **Volume indicator:** write white at value 255 with alpha decreasing toward
  the center. The retained UI layer source-overs it, revealing the scene in
  proportion to alpha.
- **Waves crossing:** each wave renders separately; the second scratch result
  source-overs the retained first-wave result. Both colors remain in the RGB
  mixture without either animation sampling or knowing about the other.
- **Wave wake over FFT:** wake value controls emitted color/intensity while its
  lower alpha controls how much FFT remains visible underneath.

### Rollout choice: all RGBA layers or opt-in RGBA layers

The clean homogeneous pipeline makes every retained layer premultiplied RGBA.
That allows source-over anywhere and keeps layer composition simple. Its risk is
adapting the legacy HSV `MaxValue`, hue-winning `AddValue`, and value-decay
semantics after a pixel becomes a true RGB mixture.

A lower-risk rollout keeps legacy layers HSV and introduces explicitly
alpha-capable RGBA layers for animations that need this behavior. Multiple
waves targeting the same RGBA layer still interact exactly as requested. This
preserves every established layer until deliberately migrated, at the cost of a
heterogeneous `LayerStack` and explicit ordering boundaries between HSV and
RGBA layers.

Because CPU cost is more important than the modest storage delta, the opt-in
rollout is the leading implementation direction. Frames with no alpha animation
remain on the exact current path: all legacy animations accumulate in HSV and
the completed scene converts once. Alpha animations alone pay scratch-to-RGB
conversion and blend sequentially into a shared RGBA retained layer. The
homogeneous all-RGBA stack should be adopted only if measured cost is acceptable
and its simpler contracts outweigh the unnecessary conversion of legacy
animations.

The selected **mechanism** is settled—premultiplied RGBA retention at
`blendScratch()` for alpha-capable layers. Compatibility and CPU priorities now
favor opt-in RGBA layers over immediate homogeneous migration.

### RAM projection

For the homogeneous form, replacing eight retained HSV frames with RGBA adds
9,216 bytes. Adding an owned alpha scratch plane adds 1,152 bytes, and a
full-logical alpha plane adds 2,304. Opaque RGB triple presentation remains
three bytes per pixel, so the total static delta is 12,672 bytes per GPU.

Applied to the measured 208,628-byte `gpu0` link, this projects to 221,300 bytes
(67.5%), leaving 106,380 bytes before implementation-dependent metadata and
alignment. An optional one-byte legacy value/comparison key for every retained
pixel would add another 9,216 bytes, projecting to 230,516 bytes (70.3%) and
still leaving 97,164 bytes. RAM is not the deciding constraint.

### CPU projection

In a homogeneous all-RGBA stack, HSV→RGB conversion moves from once per final
output pixel to once per **participating animation scratch pixel** when scratch
is accepted into its retained RGBA layer:

```text
current conversions/frame  ≈ owned pixels
RGBA conversions/frame     ≈ sum(nontransparent owned scratch pixels
                                  for each active animation)
```

For dense animations, the upper bound simplifies to:

```text
current: 1,152 conversions/frame
new:     active dense animations × 1,152 conversions/frame
```

Thus one dense animation performs roughly the same number of conversions, only
earlier. Two simultaneous dense waves double conversion count from 1,152 to
2,304 per frame, or from 144,000 to 288,000 conversions/second at 125 fps. The
increment relative to today is approximately `(dense animation count - 1) ×
1,152` conversions/frame. A sparse animation still scans its 1,152-pixel
scratch during `blendScratch()`, as it already does, but alpha-zero/value-zero
pixels can skip HSV conversion entirely. A full-logical animation converts only
the 1,152 pixels projected into the current GPU's owned scratch, not all 2,304.

Final retained-layer composition is then RGB-only and output performs no final
HSV conversion. Frames with no newly rendered animation can therefore perform
fewer conversions than today even while retained RGB layers decay/compose.

In the preferred opt-in rollout, the exact legacy HSV scene still converts once
at the final boundary, and only alpha-capable animations add early conversions:

```text
opt-in conversions/frame ≈ owned legacy-base pixels
                           + sum(nontransparent owned scratch pixels
                                 for alpha animations)
```

For an FFT base plus two dense alpha waves, that is up to 1,152 + 2×1,152 =
3,456 conversions/frame (432,000/second). When no alpha animation is active, it
falls back to the current 1,152 conversions/frame with no RGB-blend overhead.

Conversion count is not the whole CPU delta. Current final HSV layer composition
mostly scales/compares one `value` byte and selects a color. Premultiplied-RGBA
composition performs channel-wise inverse-alpha multiplication/addition for
source-over. With five enabled layers, that arithmetic can be comparable to or
larger than the moved HSV conversion cost. Alpha-zero and alpha-255 fast paths
are therefore architectural requirements, not optional micro-optimizations:
empty pixels skip, and opaque pixels copy without channel multiplies.

For legacy `MaxValue`, a retained comparison key can test whether the source
wins before converting it, avoiding HSV conversion for losing scratch pixels.
For true source-over, every nontransparent contributing source pixel needs its
RGB value. Production uses FastLED's integer HSV conversion rather than the
host generic converter, so instruction-level cost must be measured on the S3;
conversion counts alone do not establish frame-budget percentage.

Transparent-source and fully opaque-source fast paths are important. Full-frame
animations need an alpha plane projected through the same logical-to-owned map
as HSV color; they do not require an RGB full-logical scratch buffer.

### Limit of a flattened retained UI surface

Per-pixel alpha does not by itself create independently retained UI elements.
Once two widgets have been composited into one UI bitmap, their separate colors,
alphas, and ownership are lost. The flattened result can decay as a whole, but
one overlapped widget cannot later fade, update, or expire independently from
the other.

The current `clearOnPlay` policy deliberately supports one replacing UI
presentation: starting any UI animation clears the previous UI pixels. If the
intended UI remains “one current notification/menu snapshot with one shared
fade,” an alpha-bearing UI surface is sufficient. If it means multiple
concurrent widgets with independent lifetimes, implementation additionally
needs retained element instances, separate surfaces, or deterministic full UI
re-rendering from UI state each frame. That is a product/architecture decision
required before implementation; pixel alpha cannot paper over it.

## Early CPU and bandwidth model

After the master's startup layer policy, five layers are normally enabled:
`Fft`, `Effect`, `TransientEffect`, `Debug`, and `UI`. Even with one active
animation, the current pipeline performs at least:

- 5,760 layer pixels of clear/decay work per frame;
- 5,760 layer pixels of final composition per frame;
- 1,152 scratch clears and 1,152 scratch-to-layer blends per active animation;
- the animation's own pixel/field calculations (typically 2,304 logical points
  for a dense field, even though each GPU owns half).

Thus a typical one-animation frame has at least 13,824 buffer-loop iterations,
and a two-animation FFT crossfade has at least 16,128, before its 2,304/4,608
logical field evaluations. At 125 fps this is roughly 1.73–2.02 million buffer
iterations per second.

Moving four rather than three bytes increases raw pixel-buffer traffic by 33%,
but internal SRAM bandwidth is unlikely to be the limiting resource. A
four-byte aligned pixel may also compile into simpler whole-word copies than a
three-byte struct, so CPU cost cannot be inferred directly from byte count.
The arithmetic contract is more important:

- A coverage-only HSV approximation needs roughly one additional 8×8 scale per
  participating pixel and can fast-path alpha 0/255. This is unlikely by itself
  to dominate the existing dense field math.
- Conventional source-over RGB needs channel-wise blending and an effective
  per-pixel × per-animation/layer opacity. With premultiplied RGB this remains
  bounded integer math, but it changes where HSV→RGB conversion occurs.
- Performing true source-over while retaining HSV buffers requires repeated
  HSV→RGB plus RGB→HSV conversion, or an incorrect hue interpolation. That is
  the CPU-risky design and would also threaten current visual fidelity.
- Restricting true alpha to sparse UI pixels with explicit alpha-0/255 fast
  paths makes its arithmetic cost negligible relative to dense animations.

### Quantitative alpha arithmetic bounds

A complete UI-plane scan is 1,152 pixels per frame, or 144,000 pixels/second at
125 fps. Straight RGB source-over is approximately two 8-bit multiplies per
channel (source by alpha and destination by inverse alpha), so a completely
translucent full-screen UI is on the order of 864,000 channel multiplies/second,
plus adds, rounding, loads, and branches. This is not an alarming arithmetic
rate for the ESP32-S3, but it remains an estimate until measured in the real
build.

The current brightness indicator is much cheaper: composition still scans the
1,152-pixel UI plane, but at most 32 pixels are nontransparent. With alpha-zero
and alpha-255 fast paths, only 4,000 pixels/second reach the nontrivial color
blend, and the current fully opaque indicator reaches none of it.

For comparison, applying straight source-over indiscriminately to five full
layers would visit 720,000 layer-pixels/second and perform roughly 4.32 million
channel multiplies/second. That is probably still feasible in isolation, but it
is unnecessary work in the same frame budget as the dense field animations.
More importantly, converting five HSV sources to RGB can raise HSV conversion
from the current 144,000 final pixels/second to roughly 720,000 source
pixels/second. The conversion placement, not the alpha multiply itself, is the
credible CPU risk.

The initially considered UI-only composition would perform the base HSV-to-RGB
conversion once and add only one overlay scan. That estimate remains a useful
lower bound, not the corrected architecture. Global HSVA layer composition can
convert up to one source per enabled layer; global retained RGBA instead moves
conversion to active animation scratch acceptance. The current frame metrics
(`rndMax`, `encMax`, `showMax`, `stepMax`, missed strobes, and repeated presents)
are sufficient for acceptance, though a compositor-specific timing measurement
or microbenchmark would make regressions easier to localize.

### CPU conclusions

- **One general RGBA overlay:** low expected CPU impact for sparse effects and
  bounded by active overlay coverage for dense effects.
- **Alpha byte in every HSV buffer but no true RGB mix:** low arithmetic cost
  but only supports correct source-over when RGB conversion happens between
  still-independent layers.
- **True alpha on every retained animation layer:** potentially material due to
  multiplied HSV conversion and broader memory passes; measure before accepting.
- **HSV→RGB→HSV composition:** unacceptable architecture regardless of whether
  a microbenchmark happens to fit the initial frame budget.

There is no current production 32×72 on-device timing capture in this analysis,
so no design should claim a measured percentage of the 8 ms frame budget. The
bench acceptance described later is mandatory.

## Output-policy implications

The current outputs receive HSV. They first use HSV `value` for the configurable
output-value floor, convert to RGB, then apply the luma floor (after modeling
global brightness). A final-RGB presentation contract moves the conversion
earlier and therefore requires an explicit definition for the value floor:

- apply the legacy HSV value floor to the base before UI composition;
- define an RGB equivalent such as maximum channel for the completed frame; or
- keep zero as the explicit disabled setting while retaining the separately
  defined luma-floor option.

These placements differ for low-alpha UI pixels. A floor after composition can
erase a subtle antialiased edge; a floor before UI composition can allow the UI
to bypass the legacy floor. The floors are optional WS2812B-era workarounds, but
the alpha change must preserve their ability to be enabled or disabled and
must not silently reinterpret them.

Global hardware brightness remains orthogonal. UI and scene are composited in
the same color range, and the SK9822 five-bit common brightness is applied to
the completed opaque pixel. No per-pixel alpha reaches the 4,760-byte wire
frame, and SPI timing is unchanged.

## Host tooling and format impact

The host renderer is part of the rendering contract and must evolve with the
embedded path. It currently:

- constructs the same HSV `LayerStack`;
- serializes every `.tled` plane as exactly three bytes per pixel;
- explicitly writes HSV fields rather than dumping `sizeof(HsvColor)`;
- computes `bytesPerFrame` as `planeCount × pixelCount × 3`;
- has a Python reader whose array shape and strides hard-code three channels.

Consequently, adding a byte to the embedded struct would **not** make traces
capture alpha. It would instead create a silent validation gap: final RGB could
be tested, but retained UI coverage and element fade behavior could not be
inspected directly.

A global implementation should keep version-1 legacy HSV/RGB traces readable
and add enough inspectable state for the selected boundary, for example:

- versioned one-byte alpha planes paired with selected HSVA layer planes; or
- versioned four-byte RGBA planes for alpha-capable retained layers/overlays,
  with per-plane byte widths.

The second choice is more self-describing, but either requires a trace-format
version/schema change and matching C++ writer, Python reader, viewer, analyzer,
and stitch tests. Merely validating final RGB is useful but insufficient for
debugging retained alpha ownership. PubSub animation commands and SK9822 wire
frames do not need to change unless new UI animation configurations expose
alpha values.

## Value for UI

Per-pixel alpha is highly useful for UI even though the first indicator has
only binary pixels today.

### Immediate correction: retained UI fade

The current shared UI fade decays HSV `value` while the UI layer remains
`Replace`. For every nonzero step, a fading white UI pixel still replaces the
colored scene with progressively darker gray; only when value reaches zero
does the lower scene reappear. That is a fade to black followed by reveal, not
a translucent fade-out.

Retaining full UI color and authored per-pixel alpha while decaying a separate
uniform UI lifetime opacity produces the intended behavior: the underlying
animation becomes continuously more visible without thin/antialiased pixels
vanishing early. This is the strongest immediate reason to add UI alpha.

### Element-local uses

An element can use distinct per-pixel alpha even while the UI surface has a
separate uniform opacity/lifetime. That enables:

- antialiased text, icons, arcs, and diagonals when their rasterizers actually
  generate fractional coverage;
- soft selection halos, focus rings, and radial-menu sectors;
- translucent notification or menu regions that do not erase the show beneath;
- local fades, reveals, wipes, and dissolves without fading unrelated UI;
- overlap between UI shapes without abrupt hue/saturation winner changes;
- stable bright element colors whose coverage changes independently from
  luminous intensity.

Alpha does not automatically provide any of those. `Canvas` primitives and any
future glyph/icon rasterizers must compute or accept coverage, and independent
element lifetimes require the element model discussed above.

The current `RadialGauge` itself can remain alpha 255 while active. Its
shared expiry fade is what benefits. A later radial menu is the clearest
consumer of fractional edge coverage and translucent highlights.

## Value for existing animations

Most existing animations model emitted light, not painted translucent objects.
For those, HSV `value` plus `MaxValue`, `AddValue`, and retained value decay are
often the more natural controls. Adding alpha to their pixels would not improve
them automatically.

### High-value candidate outside the UI layer

`WheelIndicator` is semantically an overlay. It currently uses the existing
uniform `BlendOp::Alpha` at opacity 192 when writing its layer, and the `Wheel`
layer applies the same heuristic again at opacity 192 during final composition.
Its falloff is encoded by reducing HSV value. True per-pixel coverage could keep
the selected wheel color stable, give the edge a genuine translucent falloff,
and mix with the show without the current winner-takes-hue/saturation behavior.

That makes it a good **later opt-in migration candidate**, not justification to
change `BlendOp::Alpha` globally. Exact legacy output should remain available
until side-by-side traces and hardware viewing approve the new look.

### Moderate-value effect candidates

`Lighthouse`, `OrbitRing`, `Starburst`, and `Bolt` have beams, wakes, point
profiles, or glow widths that could optionally interpret their edge profile as
coverage when intentionally overlaid on another bright colored field. Sparse
`OrbitSparks` could similarly gain translucent ember/smoke variants. Alpha
would make those overlaps different, but not categorically more correct for an
additive light sculpture; the current value envelopes already make good soft
emission profiles.

`Sinelon` and persistent FFT/effect trails already obtain motion history from
retained layer value decay. Alpha could create a “transparent colored trail”
variant, but it is not needed to retain or fade trails.

### Little or no direct value

- `SpectralWeave`, `SpectralIris`, `StainedCells`, `Cymatic`, `Vortex`,
  `BreathingRings`, `RadialCurtain`, and `PolarLattice` are dense fields whose
  intended intensity is already their HSV value.
- `CenterWave`, `SineWave`, `Shutter`, and most `Starburst` behavior already
  computes an explicit value profile over space/time.
- `DiagnosticFill` and `SpokeSweep` should stay simple and unambiguous for
  bring-up.
- Existing FFT crossfades already use uniform layer opacity. Per-pixel alpha is
  only needed if a future transition uses a spatial mask.

The preservation rule is important: no existing animation should acquire
fractional alpha merely because its value is below 255, and the existing
compositor's results should remain byte-identical when the UI is absent.

## New animation/effect families enabled

True per-pixel alpha makes several families practical:

- spatial crossfades between two completed animations using radial, angular,
  noise, or image masks;
- local wipes/dissolves where transition progress varies per pixel;
- translucent sprites, symbols, and layered menu graphics;
- soft occluding fog/smoke or colored-glass overlays, distinct from simply dim
  emitted particles;
- subpixel/coverage animation of thin geometry whose apparent position moves
  smoothly across the discrete spoke/ring grid.

However, these do not require alpha in every stored animation pixel. A future
transition compositor can consume two opaque frames plus a separate one-byte
mask; UI sprites can remain in the dedicated UI path. Frame-feedback warping is
also not unlocked by alpha alone—it still needs retained frame history and the
appropriate full-frame or halo sampling. The architecture should add the
narrow primitive each concrete effect needs rather than globalizing alpha in
anticipation.

## Withdrawn UI-only implementation surface

This table records the surface implied by the rejected UI-only design. It must
not be used as the implementation plan; it remains here to preserve the
research trail and identify output/trace work shared by global alternatives.

| Area | Required provision | Preserve |
| --- | --- | --- |
| Color types | Add an explicit UI RGB+alpha or premultiplied-RGBA type | Keep `HsvColor` three-byte semantics and initialization |
| UI drawing | Provide explicit coverage/alpha in UI pixel and primitive APIs | Existing `Canvas` calls remain opaque HSV writes |
| Retention | Retain UI color/authored alpha and apply a separate lifetime fade according to the chosen ownership model | Existing non-UI HSV layer decay and blend behavior |
| Composition | Compose non-UI layers in HSV, convert once, then RGB source-over UI | Existing layer ordering below UI and exact no-UI output |
| Presentation | Snapshot completed opaque RGB, or equivalently snapshot every input needed for later composition | Never let presentation read a newer live UI surface |
| Outputs | Accept final RGB or a completed-frame abstraction; apply brightness/floor policy explicitly | SK9822 framing/brightness and legacy FastLED operation |
| Host renderer | Execute the same UI path and serialize inspectable UI alpha | Read existing version-1 `.tled` traces unchanged |
| Tests/metrics | Add algebra, fade, parity, format, resource, and timing coverage | Existing topology, blend, output, and trace regressions |

The UI animation interface is the main code-shape question. The common
`AnimationRenderContext` currently always exposes one HSV `Canvas`, and the
registry dispatches every animation through that signature. A UI implementation
needs either a distinct UI render context/canvas, or an explicitly optional
coverage target in the canvas. It should not make every animation pay for or
accidentally write alpha. Direct frame writes and full-logical rendering must
also have defined behavior; relying on “nonzero value implies opaque” as a
permanent hidden rule would recreate the value/coverage confusion.

## Validation and acceptance requirements

### Pure/host correctness

- Test source-over endpoints: alpha 0 is exact destination identity and alpha
  255 is exact source replacement.
- Test known intermediate RGB mixes and rounding, including black, white,
  saturated colors, transparent black, and opaque black.
- Test multiplication/order of per-pixel, animation, and layer opacity if all
  three remain supported for UI.
- Test that UI fade changes effective alpha while stored color and authored
  coverage remain stable, that antialiased edges retain their relative shape,
  and that a lower bright scene is progressively revealed rather than blacked
  out.
- Test overlapping UI elements according to the selected ownership/lifetime
  contract, including `clearOnPlay` or its replacement.
- Preserve byte-exact final HSV/RGB traces for scenes without UI alpha.
- Test both GPU ownership maps; alpha must follow the same 0..15 and 16..31
  spoke projection as color.
- Test enabled and disabled output floors at alpha edges under both output
  backends.
- Version the trace schema deliberately and test both old-trace reading and new
  per-plane offsets/strides.

### Static resources

- Rebuild at least `gpu0` and `gpu1` and record linked RAM/flash.
- Re-run object-size inspection to verify actual UI, presentation, and output
  storage against the projections in this document.
- Record on-device heap/stack low-water marks; static link headroom alone is not
  runtime proof.

### Production-code bench timing

Use the same 32×72 production geometry/output code on the 2×144 physical test
build, as required for v2 bring-up. Capture separately:

- no alpha, a sparse alpha animation, and a worst-case full-surface
  fractional-alpha animation;
- representative dense FFT/effect load and the two-animation FFT crossfade;
- `rndMax`, `encMax`, `showMax`, `stepMax`, over-budget frames, missed strobes,
  and repeated presents over a sustained run;
- output at brightness 0, low nonzero hardware levels, and full brightness,
  with floor options both off and intentionally on.

Acceptance requires the 8 ms step budget at 125 fps without hiding failures by
lowering the frame rate, plus visual approval of no-alpha parity, alpha-animation
overlaps, and UI fades on real LEDs. If timing fails, measure the new RGB/alpha
composition separately
before considering broader renderer optimizations; owned-space/halo rendering
remains a later optimization phase.

## Risks ranked

1. **Blocking architectural risk — alpha granularity:** layer-to-layer alpha
   permits retained HSVA; correct same-layer animation-to-animation alpha
   requires retained RGBA or separate surfaces. The product requirement selects
   the pipeline.
2. **High visual-regression risk — global compositor changes:** changing
   existing HSV `Alpha`, `MaxValue`, `AddValue`, or decay semantics can alter
   every established effect. No-alpha rendering needs an exact legacy contract
   and trace comparison.
3. **Medium CPU risk — conversion placement:** late HSVA composition can
   multiply HSV conversion by enabled layer count; early RGBA retention converts
   active animation scratch and changes retained blend semantics.
4. **Medium lifetime risk — flattened alpha surfaces:** contributions cannot be
   independently removed after they are composited into one retained layer.
5. **Medium contract risk — HSV/RGB output boundary:** floor placement and
   presentation snapshot ownership must be explicit.
6. **Medium validation risk — host trace blind spot:** a three-channel trace
   cannot diagnose retained alpha even if final RGB looks plausible.
7. **Low RAM risk for layer/overlay designs; high for per-animation surfaces:**
   four-byte layer pixels fit comfortably, while 32 independent RGBA animation
   frames consume 147,456 bytes before the rest of the display system.

## Research log

- 2026-08-27: Read the current overview, command reference, and animation
  pipeline documentation. Identified the newly added retained `UI` layer and
  `RadialGauge` animation.
- 2026-08-27: Traced `HsvColor`, `Canvas`, `LayerStack`, `Compositor`, renderer
  adapters, and the current uniform-opacity `BlendOp::Alpha`. Established that
  a fourth byte alone would not define correct compositing behavior.
- 2026-08-27: Calculated exact whole-pipeline and UI-only RAM deltas. Audited
  enabled-layer, scratch, compose, and dense-render loops to establish the
  buffer-iteration floor at 125 fps and separate memory traffic from color-space
  conversion risk.
- 2026-08-27: Built the current `gpu0` image with the new UI layer and measured
  linked RAM, flash, and debug object layouts. Applied the exact candidate
  storage deltas to that baseline; all variants retain substantial static RAM
  headroom.
- 2026-08-27: Traced presentation buffering and both output backends. Identified
  the need to snapshot final RGB (or all composition inputs), and the enabled
  value-floor policy decision created by moving HSV conversion earlier.
- 2026-08-27: Audited current animation roles and blend styles. Classified UI
  fade and `WheelIndicator` as the strongest alpha consumers, while dense
  emissive fields already have appropriate value semantics.
- 2026-08-27: Audited `.tled` serialization and the Python reader. Recorded the
  versioned trace work required to prevent alpha from becoming invisible to
  host regression tools.
- 2026-08-27: Compared global HSVA, value masks, all-RGB layers, UI-only
  composition, and specialized masks. The initial pass selected UI-only
  post-HSV RGB composition as the lowest-risk direction and documented its
  decision gates and acceptance suite.
- 2026-08-27: Owner rejected UI-only scope because animations, not UI, are the
  valuable consumer. Reopened the analysis, withdrew that recommendation, and
  separated HSV animation authoring from RGB alpha composition.
- 2026-08-27: Added four global-animation alternatives: late HSVA layer
  composition, a general RGBA animation overlay, RGBA retention for every
  layer, and independent per-animation surfaces. Identified same-layer versus
  layer-to-layer source-over as the blocking semantic distinction.
- 2026-08-27: Owner clarified that existing sequential scratch-to-layer blending
  already supplies animation ordering. With shared scratch cleared per
  animation, the later wave can alpha-blend into the retained result without
  animation-private state. Withdrew separate composition lanes as the core
  mechanism and selected HSV+alpha scratch → premultiplied-RGBA retained layer
  at `blendScratch()`.
- 2026-08-27: Refined the CPU model: conversion changes from once per final
  owned pixel to once per nontransparent owned scratch pixel per active
  animation. Recorded dense one/two-animation bounds and identified RGB layer
  source-over arithmetic plus fast-path hit rates as co-equal timing factors.
