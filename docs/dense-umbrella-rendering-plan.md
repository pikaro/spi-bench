# DenseUmbrella Rendering Expansion Plan

Status: **implemented through host and firmware build validation; physical bench acceptance pending**  
Date: 2026-08-26  
Geometry updated: 2026-08-27  
Source analysis: `docs/dense-umbrella-rendering-analysis.md`

## Outcome

Both GPU firmware images use the production 32-spoke × 60-ring geometry and
render one contiguous 960-pixel half each. The animation, layer, compositor,
and present-buffer pipeline remains HSV-based and visually unchanged. A
compile-time-selected output backend converts the owned HSV frame with the
existing FastLED color conversion, encodes ordinary SK9822 frames with one
global 5-bit hardware brightness, and transmits them through ESP-IDF SPI3.

The initial implementation keeps 125 fps, disables temporal dithering for
SK9822, retains the existing configurable output floors with zero disabling
them, and does not introduce `SK9822HD`, animation retuning, owned-halo
optimization, mixed output on one MCU, or a reduced test geometry.

## Implementation checkpoint — 2026-08-26

Work packages 0 through 5 are implemented. Work packages 6 and 7 deliberately
remain physical/on-device acceptance work rather than inferred completion.

- The legacy `gpu0` and `gpu1` baselines linked at 164,112 bytes of reported
  RAM each. All 23 captured legacy animation traces remained byte-identical
  after the topology and output-boundary refactors.
- `DenseUmbrella`, production ownership, topology selection, and host profiles
  are implemented. `bin/test-led-render-stitch` rendered all 23 fixtures as
  dense-full, dense-gpu0, and dense-gpu1 and proved that neither GPU writes
  outside its half and that the two halves reconstruct every full trace plane.
- `Display` now depends on a compile-time `SelectedOutput`. The legacy FastLED
  WS2812B backend remains available, while the v2 GPU builds select the direct
  SK9822 SPI backend without Arduino `SPIClass`.
- The pure encoder emits a 3,968-byte frame for 960 pixels and host tests
  cover all 256 logical brightness inputs, all six RGB byte orders, start/end
  framing, black frames, invalid sizes, and value/luma floor behavior at the
  emitted hardware level. RGB bytes are not globally pre-scaled.
- The ESP-IDF transport owns one aligned static DMA frame, uses SPI3 mode 0 at
  12 MHz with no CS/MISO and queue depth one, and preserves DMA-buffer ownership
  across bounded wait failures. Metrics now separate encode (`encMax`), queue
  (`spiQMax`), wait (`spiWMax`), show, render, and total frame durations.
- With the 2026-08-27 32×60 geometry, production `gpu0` and `gpu1` builds pass
  at 198,228 bytes (60.5%) and 198,548 bytes (60.6%) of reported RAM.
  `SelectedOutput` is 4,032 bytes, including the 3,968-byte encoded frame. The
  configured task stacks total 43,008 bytes.
- The v2 GPU/master pin assignments match
  `schematic/build/perfboard-v2.config-netlist.json`. GPU1 owns the shared
  active-low gate on GPIO9. R22 is verified as a 10 kΩ pull-up from `LED_EN` to
  `GPU1_3V3`, so the four 74AHCT125 outputs remain disabled during cold reset.
- Both GPUs start at logical brightness zero. GPU1 drives the gate inactive
  before its normal boot delay, leaves it inactive after local output setup,
  and exposes a manual `/led-gate` command for the first bench. This is not yet
  an unattended two-GPU readiness protocol and is not display blanking.
- The master source pin map is aligned. Its debug-only WiFi, network commands,
  and UDP PubSub path had been active accidentally despite WiFi being excluded
  from the master build components. Per owner direction, that entire path is
  now commented out completely; WiFi is intended to move to the future
  power/metrics node because loaded WiFi disrupts master SPI. The resulting
  SPI/RS485-only master build passes at 195,960 bytes (59.8%) of reported RAM
  and 1,099,475 bytes of flash.
- The BTF strip's wire color order remains deliberately provisional (`BGR`)
  until the primary-color bench test. No firmware was uploaded and no claims
  are made yet about 125 fps, waveform margin, startup flashes, lowest useful
  brightness, waterproofing, or full-chain integrity.

## Settled contracts

### Geometry and ownership

- Logical dimensions are 32 spokes × 60 radial LEDs = 1,920 pixels.
- The production geometry has a roughly 60 mm inner radius. Sixty pixels at
  144 LEDs/m occupy approximately 417 mm radially. Physical dimensions belong
  to the topology; the legacy WS2812B topology retains its 300/300 mm model.
- LED numbering starts at the center of each spoke.
- The physical map is:

  ```text
  physical = spoke * 60 + (odd(spoke) ? 59 - radial : radial)
  ```

- GPU0 owns spokes 0..15, physical pixels 0..959, local pixels 0..959.
- GPU1 owns spokes 16..31, physical pixels 960..1919, local pixels 0..959.
- There is one physical SK9822 chain per GPU. Power injection at each spoke
  does not change logical or signal-chain ownership.
- The 2×120-LED bench runs this exact geometry, ownership, buffer sizing,
  encoder, and 960-pixel transmission. It merely has no physical receivers
  after the first two spokes of each half.

### Rendering and visual behavior

- Animations continue to receive the same logical canvas and produce HSV.
- Layer allocation, blending, composition, rotation, animation lifetimes, and
  triple buffering remain unchanged.
- The selected FastLED HSV-to-RGB conversion remains the production color
  transfer during migration.
- Existing animation constants are not retuned merely because the surface is
  denser. Visual tuning is a separate, later change supported by trace and
  hardware comparison.
- No optimization that changes which points an animation evaluates belongs in
  initial bring-up.

### Brightness, blanking, and dithering

- Public display brightness remains 0..255; protocol quantization occurs once
  in the SK9822 encoder.
- Initial SK9822 mapping deliberately matches FastLED 3.10.3:

  ```text
  level = floor(value * 31 / 255)
  if value != 0 and level == 0: level = 1
  ```

  Thus 0→0, 1..16→1, every hardware code 1..31 remains reachable, and
  255→31.
- One common 5-bit level is written to every pixel header. RGB bytes are not
  globally pre-scaled; that would apply brightness twice.
- Temporal dithering is disabled for SK9822.
- `outputValueFloor` and `outputLumaFloor` remain explicit configuration
  options. Zero disables each option. This plan does not select new defaults.
- User brightness and display on/off are separate concepts. A future rotary
  `ButtonMenu` action will blank the display without forgetting its nonzero
  brightness. Logical brightness zero remains a supported low-level off value
  for compatibility, startup, testing, and failure handling.
- Shared active-low `LED_EN` gates the 74AHCT125 signal outputs; it does not
  blank pixels that already latched a frame and must not be used as the menu's
  display-off control.

### Transport

- PubSub remains an ESP-IDF SPI2 slave on each GPU.
- LED output uses the otherwise free ESP-IDF SPI3 master peripheral.
- Initial LED transport is mode 0, MSB-first, no MISO, no CS, queue depth one,
  and 12 MHz. A 10 MHz fallback remains available for signal-integrity work.
- The implementation does not enable FastLED's Arduino `SPIClass` backend and
  does not add Arduino as a framework or dependency.
- Initial `show()` may queue and immediately reap one transaction. The
  transport contract must allow queue/reap separation later if measurement
  proves that overlap is required.

## Target code boundaries

```text
Display
  -> AnimationEngine / LayerStack / Compositor / PresentBuffers (unchanged)
  -> compile-time SelectedOutput
       -> Ws2812bFastLedOutput (legacy path)
       -> Sk9822SpiOutput
            -> existing HSV-to-RGB renderer
            -> output-floor policy
            -> pure Sk9822Encoder
            -> ESP-IDF Spi3Transport
```

The output concept remains the existing small surface:

```cpp
begin(const LedDisplay::Config &)
setBrightness(uint8_t)
show(std::span<const HsvColor>)
deinit()
```

Use a compile-time alias or policy selection, not virtual dispatch. Do not make
animations, layers, or the compositor aware of the LED protocol.

## Work package 0 — freeze the visual and resource baseline

### Changes

1. Add a focused host test entry point, proposed as `bin/test-led-display`, for
   pure topology, ownership, brightness, floor, and encoder tests.
2. Extend the offline renderer build wrapper so it can compile explicit
   legacy-full, dense-full, dense-gpu0, and dense-gpu1 profiles. These are host
   verification profiles, not persistent PlatformIO environments and not
   alternate firmware geometries.
3. Capture deterministic legacy traces for every checked-in animation example,
   representative rotation offsets, multiple simultaneous effects, and the
   `StainedCells`↔dense-FFT crossfade case.
4. Record current `gpu0` and `gpu1` build sizes and relevant static object sizes.

### Files

- `bin/test-led-display` (new)
- `test/led_display_logic.cpp` (new)
- `bin/led-render-build`
- `bin/led-render`
- `tools/led-render/HostRuntime.hpp`
- `tools/led-render/host_render.cpp`
- `tools/led-render/README.md`

### Exit criteria

- Legacy `gpu0` and `gpu1` build before structural changes.
- The complete legacy trace suite is reproducible from recorded commands.
- Tests can compile more than one topology/ownership profile without changing
  production source files by hand.
- Baseline artifacts record configuration and checksums; large generated trace
  files need not be committed if the commands and deterministic hashes are.

## Work package 1 — make topology selection explicit

### Changes

1. Make the legacy `Umbrella` mapping a self-contained topology type rather
   than having it read selected global geometry constants.
2. Add `detail::DenseUmbrella` with its own 32×60 constants and monotonic
   serpentine map.
3. Select the active topology in `LedTopology/Facade.hpp`/static configuration
   and expose it under a neutral name such as `LedTopology::Surface`.
4. Change `AnimationEngine` and host-render map creation from the concrete
   `LedTopology::Umbrella` name to the selected surface. No animation API
   changes are required.
5. Keep `OwnedPixels` as the physical-to-local ownership mapping. Remove its
   conceptual dependence on output data-line count; legacy WS2812B line
   subdivision belongs in the legacy output backend.
6. Select two equal physical groups and one owned group per GPU for v2.

### Files

- `include/LedTopology/detail/Umbrella.hpp`
- `include/LedTopology/detail/DenseUmbrella.hpp` (new)
- `include/LedTopology/Facade.hpp`
- `include/LedTopology/detail/OwnedPixels.hpp`
- `include/StaticConfig/LedDisplay.hpp`
- `include/LedDisplay/detail/AnimationEngine.hpp`
- `tools/led-render/HostRuntime.hpp`
- `platformio.ini`

### Host tests

- Exhaustively prove `physicalFor()` is in range and bijective over all 1,920
  logical points.
- Prove inverse radial mapping for every pixel.
- Assert these DenseUmbrella boundaries:

  ```text
  (spoke 0, radial 0)  -> physical 0
  (spoke 0, radial 59) -> physical 59
  (spoke 1, radial 0)  -> physical 119
  (spoke 1, radial 59) -> physical 60
  (spoke 15, radial 0) -> physical 959
  (spoke 16, radial 0) -> physical 960
  (spoke 31, radial 0) -> physical 1919
  ```

- Exhaustively prove GPU0/GPU1 ownership, local-index range, and
  `physicalIndex(localIndex(p)) == p` for owned pixels.
- Cover zero, half-turn, near-wrap, and full-wrap rotation offsets.
- Render dense-full, dense-gpu0, and dense-gpu1 with identical inputs and prove
  the stitched half traces equal the full trace byte-for-byte.

### Exit criteria

- All topology/ownership tests pass for legacy and dense selections.
- The protocol-neutral refactor produces byte-identical legacy logical traces.
- Each v2 GPU links with exactly 960 owned pixels and 1,920 logical pixels.

## Work package 2 — select output backends cleanly

### Changes

1. Introduce an output selector/facade with a compile-time backend choice.
2. Keep the existing FastLED controller path as the legacy WS2812B backend;
   rename it only if the rename stays mechanical and trace-neutral.
3. Move the two-line limit and line subdivision out of `Display` and into the
   WS2812B backend.
4. Change `Display` to hold `Outputs::SelectedOutput` while retaining its
   existing lifecycle/frame calls.
5. Add an SK9822-specific runtime config containing SPI host, data pin, clock
   pin, clock rate, wire color order, and transfer timeout. Validate all fields
   during `begin()`.
6. Keep output floors in the output layer. Do not move them into animations or
   the compositor.

### Files

- `include/LedDisplay/Outputs/FastLedOutput.hpp`
- `include/LedDisplay/Outputs/Select.hpp` (new)
- `include/LedDisplay/Outputs/Sk9822SpiOutput.hpp` (new skeleton)
- `include/LedDisplay/Interfaces/Config.hpp`
- `include/LedDisplay/detail/Display.hpp`
- `include/StaticConfig/LedDisplay.hpp`

### Exit criteria

- Legacy WS2812B builds still compile and match baseline traces.
- Dense SK9822 builds select the new backend without compiling or registering a
  FastLED SPI controller.
- `Display`, `AnimationEngine`, animations, layers, and present buffers contain
  no SK9822/SPI-specific branches.
- No runtime heap allocation or virtual dispatch appears in the frame hot path.

## Work package 3 — implement and prove the pure SK9822 encoder

### Changes

1. Add a pure encoder independent of ESP-IDF and physical I/O.
2. Preserve the existing selected FastLED HSV-to-RGB conversion.
3. Quantize brightness once with the settled FastLED-compatible
   floor-plus-nonzero-clamp mapping.
4. Encode the initial compatibility framing used by FastLED 3.10.3:

   ```text
   start: 4 × 0x00
   pixel: (0xE0 | globalLevel), colorByte0, colorByte1, colorByte2
   end:   4 × (pixelCount / 32 + 1) bytes of 0x00
   ```

   For 960 pixels the exact transfer size is 3,968 bytes.
5. Make wire color order explicit. Use the primary-color hardware test to
   finalize the BTF strip's order in one configuration value.
6. For an enabled output value floor, derive its threshold from the emitted
   5-bit level rather than the original 8-bit request. For an enabled luma
   floor, compare against RGB scaled by the emitted level/31. Zero bypasses
   each filter completely.
7. Keep ordinary global encoding separate from transport so a later HD encoder
   can be added without changing rendering or SPI ownership. Do not implement
   HD now.

### Files

- `include/LedDisplay/Outputs/detail/Sk9822Encoder.hpp` (new)
- `include/LedDisplay/Outputs/Sk9822SpiOutput.hpp`
- `test/led_display_logic.cpp`

### Host tests

- Exact encoded size and bytes for 0, 1, 31, 32, and 960 pixels.
- All 256 logical brightness inputs, including exact boundaries and the
  accepted 1..16→1 behavior.
- Header prefix bits are always `111`; level is always 0..31.
- Brightness is not applied to RGB a second time.
- Black, white, red, green, blue, and a representative HSV corpus match the
  selected FastLED renderer and configured wire order.
- Value/luma floors disabled and enabled, including level 0 and level 1.
- Short/oversized destination buffers and wrong frame sizes fail without a
  partial transfer.
- Input HSV storage is not mutated.

### Exit criteria

- Pure tests prove every emitted byte without ESP32 hardware.
- The encoder performs no allocation and has a fixed, documented O(pixelCount)
  cost.
- Ordinary SK9822 encoding is the only production v2 encoding policy.

## Work package 4 — add the ESP-IDF SPI3 transport

### Changes

1. Add an output-private ESP-IDF transport instead of broadening the shared
   PubSub `Wire::Spi` abstraction, whose current master contract requires MISO
   and CS and has unrelated protocol/task semantics.
2. Initialize SPI3 with MOSI=data, SCLK=clock, MISO=-1, DMA auto-selection, and
   `max_transfer_sz` equal to the fixed encoded frame size.
3. Add one SPI device with CS=-1, mode 0, MSB-first, queue depth one, and the
   configured 12 MHz clock.
4. Store the transaction descriptor for at least the complete queued lifetime.
5. Use one output-owned, `alignas(4)` fixed frame buffer and verify at `begin()`
   that ESP-IDF reports it DMA-capable. Fail initialization rather than allowing
   a hidden per-frame bounce allocation/copy.
6. Initially queue and reap the transaction in the same `show()` call. Expose
   queue/reap as separate internal operations so an asynchronous overlap change
   is local if timing later requires it.
7. On timeout or transfer failure, return a bounded error, count it, and do not
   reuse the buffer while a transaction may still own it.
8. During deinit, reap/cancel any owned transaction as supported, remove the
   device, and release SPI3 in reverse initialization order.

### Files

- `include/LedDisplay/Outputs/detail/platform/Sk9822SpiESP32.hpp` (new)
- `include/LedDisplay/Outputs/Sk9822SpiOutput.hpp`
- `include/LedDisplay/detail/Metrics.hpp`

### Instrumentation

Record bounded maximum durations separately for:

- HSV/filter/encode
- SPI queue submission
- SPI completion wait/wire time
- complete output `show()`
- complete render/present task step

Retain the existing missed-strobe, repeated-frame, output-failure, and
over-budget counters. Keep instrumentation allocation-free and cheap enough for
the 125 Hz path.

### Exit criteria

- Both GPU images build under ESP-IDF without `Arduino.h`/`SPI.h` dependencies
  in the SK9822 transport.
- SPI2 PubSub and SPI3 LED output initialize independently.
- A logic analyzer sees mode-0, MSB-first, 12 MHz output with one contiguous
  3,968-byte transaction per presentation.
- Measured wire time is consistent with approximately 2.65 ms at 12 MHz.

## Work package 5 — align v2 role configuration and safe startup

### Changes

1. Update the GPU build ownership selection:

   ```text
   gpu0: groupCount=2, nodeGroups={0}, one SK9822 chain
   gpu1: groupCount=2, nodeGroups={1}, one SK9822 chain
   ```

2. Move all stale v1 PubSub/strobe pins to the generated v2 netlist values:

   | Role | PubSub MOSI/MISO/CLK | CS | ATTN | Strobe | LED CLK/DATA |
   |---|---|---|---|---|---|---|
   | GPU0 | 11/13/12 | 1 | 2 | 10 | 7/8 |
   | GPU1 | 3/1/2 | 5 | 6 | 4 | 10/13 |

3. Align the master-side v2 high-speed pins from the same generated netlist in
   the board bring-up change; do not treat stale source assignments as pin
   conflicts.
4. On GPU1, claim GPIO9 and drive shared active-low `LED_EN` inactive/high as
   early as the platform permits. Keep it inactive through SPI initialization
   and initial frame preparation.
5. Before enabling the gate, require both GPU output pins to be configured,
   both encoders to have a deliberate black/level-0 frame prepared, and an
   explicit bring-up readiness decision. For the first bench session this may
   be a manual GPU1 command after observing both ready logs; unattended
   production must replace that operator step with a master-mediated two-GPU
   readiness acknowledgement.
6. After gate enable, present black first, then a low-brightness diagnostic
   frame. Never start with the configured animation at uncontrolled brightness.
7. Treat gate disable as signal isolation only. Display blanking must transmit
   an off frame before any optional gate disable.

### Files

- `platformio.ini`
- `src/gpu/config.hpp`
- `src/gpu/main.cpp`
- `src/master/config.hpp` (v2 pin alignment dependency)
- a small output-gate owner under `LedDisplay` or GPU board setup, chosen so
  only GPU1 can drive GPIO9
- `docs/overview.md`

### Exit criteria

- Generated-netlist pins and source configuration agree for master, GPU0, and
  GPU1.
- Reset, staggered GPU boot, GPU0 absent, GPU1 absent, and reconnect tests do
  not create clock bursts or a full-brightness frame when the gate is enabled.
- The first intentionally visible frame is low-brightness and diagnostic.

## Work package 6 — exact-code 2×120 bench acceptance

### Electrical and protocol sequence

1. Build and flash the production `gpu0` and `gpu1` environments.
2. Attach 120 LEDs to each GPU: that GPU half's first two complete 60-pixel
   spokes.
3. Confirm each GPU still encodes/transmits all 960 owned pixels and all
   3,968 bytes per frame.
4. With `LED_EN` inactive, verify clock/data idle levels and absence of output
   bursts during reset and initialization.
5. Enable the gate using the selected readiness path and present black.
6. Validate start frame, representative pixel headers/data, trailing frame,
   byte count, clock polarity/phase, and continuous transaction cadence.
7. Show center/outer markers and a single moving pixel across the spoke-0/1 and
   spoke-16/17 boundaries. Confirm the odd-spoke radial reversal.
8. Run primary colors to determine/finalize BTF wire color order.
9. Exercise hardware levels 0..31 and representative logical 0..255 boundary
   values. Confirm nonzero brightness never maps to off.
10. Exercise value/luma floor options at zero and nonzero settings without
    changing their defaults.
11. Test 10 MHz and 12 MHz. Keep the highest rate that is clean with margin;
    do not test 20/40 MHz merely to maximize a number.

### Visual and safety acceptance

- Observe the lowest several nonzero global levels in realistic dusk, evening,
  and deep-night conditions.
- Confirm normal brightness changes preserve RGB relationships and do not show
  temporal dithering.
- Confirm black/off, reset, reflash, cable reconnect, and power-cycle behavior
  do not produce an uncontrolled bright flash.
- Validate connectors, power injection, sealing, and waterproof construction on
  the physical prototype. The code path is production-identical; only
  downstream physical LED coverage differs.

## Work package 7 — 125 fps computational acceptance

Run both production GPU images for every animation and the realistic maximum
concurrent layer mix. Include the orchestrated 10-second FFT crossfades, with
`StainedCells` paired with another dense FFT animation as the primary worst
case.

Record per GPU:

- linked internal RAM and object-size changes
- free heap/low-water measurements and display-task stack high water
- render, encode, SPI queue, SPI wait/wire, output-show, and total-step maxima
- missed present strobes, repeated frames, over-budget frames, and failures
- active animation count for each sample

### Exit criteria

- Sustained 125 Hz operation has no missed strobes, repeated frames caused by
  compute/output delay, transfer failures, or allocation failures under the
  complete test matrix.
- The total task step remains inside the 8 ms budget for the tested worst-case
  animation mix.
- RAM and task-stack measurements leave documented positive headroom. Do not
  reduce buffers merely to improve a percentage while real headroom is safe.
- Both GPUs remain synchronized through repeated crossfades and control
  changes.

## Work package 8 — optimize only if a measured gate fails

Apply these in order and only to the failed dimension:

1. **Wire wait dominates:** keep one DMA transaction in flight while rendering
   the next immutable HSV frame; reap it before reusing the encoded buffer.
2. **Dense logical evaluation dominates:** precompute an owned logical-point
   iteration table, then prove stitched GPU traces remain byte-identical to the
   full logical render for every regression fixture, including rotation.
3. **A specific animation dominates:** optimize only that algorithm with trace
   proof; do not retune its visual constants as a performance workaround.
4. **Frame rate still fails:** evaluate a lower synchronized frame rate only
   after the preceding measurements and optimizations are documented.
5. **ESP32-S3 remains insufficient:** preserve the same topology/output
   contracts for the future four-GPU or ESP32-P4 build rather than introducing
   a one-off renderer architecture.

## Deferred follow-up — rotary ButtonMenu display blanking

This is deliberately separate from initial LED transport bring-up.

1. Add a display-enabled command/state without replacing the existing
   brightness value.
2. Store the last selected nonzero brightness independently of enabled state.
3. On disable, transmit an explicit frame with hardware level 0 and black RGB
   bytes on both GPUs while leaving rendering state intact.
4. On enable, transmit the current rendered frame at the remembered brightness.
5. Map the rotary encoder `ButtonMenu` action to this command on the master.
6. Keep `LED_EN` active during ordinary on/off operation so both chains can
   receive the blank and restore frames.
7. Test repeated toggles, toggles during crossfade, node reconnect, and
   brightness changes while blanked.

Adding this command changes the PubSub/wire surface and should be its own
reviewed change with regenerated wire artifacts. It is not required to prove
SK9822 output and brightness in the first bench phase.

## Documentation updates before completion

- `docs/overview.md`: 32×60 surface, half ownership, v2 pins, SPI2/SPI3 split,
  and shared gate behavior.
- `docs/animation-pipeline.md`: selected topology and selected output boundary;
  animations remain protocol-neutral.
- `docs/commands.md`: rename “FastLED brightness” to “display brightness” and
  document the accepted logical-to-hardware mapping.
- `tools/led-render/README.md`: topology/owner profiles and stitch regression.
- This plan: record measured clock, color order, memory/timing results, and any
  approved deviation as work completes.

## Review/commit sequence

Keep each slice independently buildable and reviewable:

1. Baseline harness and traces only.
2. Topology selection plus DenseUmbrella and ownership tests.
3. Output selection refactor with legacy output still active.
4. Pure SK9822 encoder and exhaustive host tests.
5. ESP-IDF SPI3 transport and metrics.
6. v2 role/pin configuration and guarded output enable.
7. Bench-derived fixes limited to framing, order, clock, and startup.
8. Performance optimization only if the acceptance evidence requires it.
9. Documentation and final measured acceptance record.

Do not combine an animation aesthetic change with any of these slices.

## Definition of done

- DenseUmbrella mapping and two-half ownership are exhaustively host-tested.
- Legacy protocol-neutral traces remain unchanged after refactoring.
- GPU0 and GPU1 use the same production firmware geometry exercised by the
  2×120 physical bench.
- Each GPU emits a verified 960-pixel ordinary-SK9822 frame over SPI3 with
  hardware global brightness, no temporal dithering, and no Arduino SPI path.
- Brightness mapping, RGB order, framing, output floors, and startup/blanking
  behavior are explicitly tested rather than inferred.
- The full animation/crossfade matrix meets 125 fps with measured RAM/CPU
  headroom, or any optimization/degradation is separately justified by those
  measurements.
- Existing animations have not been retuned or visually altered during the
  migration.
- The full-chain electrical/power test remains a named gate after the 2×120
  computational/waterproof prototype; the short physical bench cannot prove
  960-device signal integrity or complete-installation power behavior.
