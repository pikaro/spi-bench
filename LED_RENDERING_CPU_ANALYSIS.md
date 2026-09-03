# LED Rendering Pipeline CPU Analysis

Status: **complete**  
Started: 2026-08-29 (Europe/Berlin)  
Completed: 2026-08-29 (Europe/Berlin)

## Objective

Review the LED rendering pipeline end to end after the LED-count increase,
identify the cause of the missed 125 Hz present strobes, and rank the quickest
and safest ways to restore timing margin without changing the rendered output.

## Executive conclusion

The immediate failure is not core-1 saturation. It is a serialized critical
path:

```text
encode + wait for 2.645 ms of SPI wire time + render/composite > 8.000 ms
```

The supplied monitoring windows still show 31–39% core-1 idle time while frame
steps miss their deadlines. `Sk9822SpiOutput::show()` queues a DMA transfer and
then blocks until the entire 3,968-byte transfer finishes before rendering the
next frame. Removing that trailing wait, while retaining the existing reap at
the start of the next `show()`, should overlap almost all of the wire time with
rendering. This is the quickest and safest way to stop the missed strobes. It
recovers about **2.65 ms of wall-clock margin per frame**, but it is a scheduling
win rather than a reduction in CPU cycles.

For actual CPU-cycle headroom, the largest confirmed waste is that each split
GPU evaluates dense animations across all 1,920 logical pixels and discards the
960 pixels owned by the other GPU only after doing the geometry and animation
math. The next best low-risk cycle win is specializing compositor loops once
per span instead of switching blend operation and scaling full-opacity pixels
inside every pixel iteration.

Recommended order:

1. make SPI presentation one-transaction-in-flight;
2. hoist compositor invariants and add exact equivalence tests;
3. evaluate dense animations only for locally owned logical pixels, then cache
   invariant field coordinates;
4. add empty-layer tracking only if measurements still justify it.

Do not start by increasing the SPI clock, reducing frame rate, moving hot data
to PSRAM, or weakening animations. Those choices have greater electrical,
behavioral, or performance risk than the confirmed structural wins above.

## Runtime evidence supplied

- The LED worker repeatedly exceeds its `8,000 us` frame-work budget, commonly
  taking about `8.1–9.3 ms` and peaking at `10.034 ms` in the sample.
- Present strobes are missed with `pending=2`; `totalMissed` rises from 411 to
  524 in the shown interval.
- At timestamp 902018, the short monitoring window reports `LedDisplay` at
  69.06% and `IDLE1` at 31.00%. The later window reports `LedDisplay` at 61.04%
  and `IDLE1` at 38.79%. There is therefore substantial idle CPU while the
  wall-clock frame step is late.
- A rough cross-check is consistent with the code path: approximately 69% of a
  core at roughly 111 processed frames/s is about 6.2 ms CPU/frame; adding the
  fixed 2.65 ms SPI wait gives about 8.85 ms, inside the observed range. This is
  an inference, not a substitute for target profiling.
- The 10-second fade begins at 899067. During it, outgoing `kind=4` is
  `SpectralIris` on the FFT layer and incoming `kind=6` is `StainedCells` on the
  alternate FFT layer. The second dense render increases load as designed.
- Overruns continue after the fade completes at 909070, with only
  `StainedCells` plus occasional `kind=2` `CenterWave` transients. The crossfade
  is an amplifier, not the root cause.
- Memory values remain stable throughout the snapshots. No allocation pressure,
  fragmentation trend, or leak is visible.

## Configuration and scaling

- Current production topology is `32 spokes × 60 rings = 1,920` logical LEDs.
- `LED_GROUP_COUNT=2`; GPU0 and GPU1 each own and transmit 960 LEDs.
- The preceding configuration visible in source history was `16 × 46 = 736`
  logical LEDs and 368 LEDs per GPU. Both full-logical animation work and
  per-GPU buffer/output work therefore grew by approximately **2.61×**.
- Target cadence is 125 fps, giving an `8,000 us` interval.
- ESP32-S3 release builds already select performance optimization, a 240 MHz
  CPU, and an 80 MHz flash clock. There is no safe compiler/clock toggle left
  to recover the missing margin.
- The display task is priority 3 on core 1. Its stack has comfortable headroom
  in the supplied sample; scheduling priority and stack size are not implicated.

## End-to-end pipeline map

```text
125 Hz present-strobe ISR
  -> increment atomic pending-strobe count
  -> notify LedDisplay task
  -> task drains/coalesces pending count and records missed strobes
  -> present previously completed HSV frame
       -> reap prior SPI transaction, if any
       -> clear/encode SK9822 DMA buffer (HSV -> RGB -> wire order)
       -> queue SPI3 DMA transaction
       -> wait synchronously for transaction completion       [main wall-time loss]
  -> render next HSV frame
       -> drain animation commands and update crossfade state
       -> beginFrame: clear/decay every enabled layer
       -> snapshot FFT/peak/wheel input and smooth audio controls
       -> for every active animation
            clear 960-pixel scratch
            run animation (many iterate all 1,920 logical pixels)
            map logical pixel to local ownership
            blend all 960 scratch pixels into its layer
       -> clear 960-pixel output frame
       -> compose enabled layers over all 960 output pixels
       -> expire completed animations and update metrics
  -> publish completed HSV frame through triple-buffer handoff
```

Triple buffering correctly separates the frame being rendered from the frame
being presented. It does not provide SPI/render overlap because `show()` waits
immediately after queueing the transfer.

## Stage-by-stage review

### 1. Present strobe, notification, and scheduling

The ISR performs an atomic increment and task notification. The task-side
pending counter deliberately coalesces notifications and reports the excess as
missed strobes. `pending=2` is a consequence of a frame step crossing the next
8 ms boundary, not evidence of expensive interrupt handling.

The present-first order should remain: it launches both GPUs' visible transfers
near the synchronized strobe. Rendering before present would add render jitter
to launch time. Asynchronous SPI preserves present-first synchronization while
allowing the subsequent render to run during the transfer.

**Assessment:** keep the ISR and ordering; no meaningful CPU win here.

### 2. Frame ownership and handoff

The renderer uses three HSV buffers, and the output driver owns a separate,
aligned encoded DMA buffer. The buffer used by an in-flight transaction is not
the buffer the renderer mutates. `show()` already waits/reaps an earlier
transaction before encoding into that DMA buffer, and `deinit()` also reaps it.

That ownership makes a single in-flight transaction a small change:

1. keep the leading `_waitForTransfer()` before reusing the encoded buffer;
2. encode and queue the new transaction;
3. return immediately after a successful queue;
4. discover a delayed transfer error at the next present/reap.

Initial black-frame presentation may remain synchronous during startup. The
only material semantic change is that an SPI completion error can be reported
one frame later, at most about 8 ms at the current cadence.

**Assessment:** buffer lifetimes already support safe overlap.

### 3. SK9822 encoding and SPI transport

The 960-LED wire buffer is exactly 3,968 bytes:

```text
4-byte start frame + 960 × 4-byte LED frame + 124-byte end frame
```

At 12 MHz, its minimum transfer time is:

```text
3,968 bytes × 8 / 12,000,000 = 2.6453 ms
```

That is 33.1% of the entire frame interval. DMA prevents the CPU from bit
banging, but the immediate wait still places the whole wire time on the task's
critical path. One queued transaction is sufficient: 2.65 ms is well below the
8 ms interval, and the leading reap prevents buffer reuse if a transfer runs
long.

Two smaller encoder inefficiencies are confirmed:

- the complete 3,968-byte buffer is zero-filled even though the following loop
  overwrites all 3,840 pixel bytes; only the 4 start and 124 end bytes need
  clearing each frame;
- wire color order is invariant for an output instance but is selected inside
  the pixel loop.

These are safe micro-optimizations after overlap is implemented. They save
redundant stores and branches but will not approach the 2.65 ms overlap gain.
Increasing SPI above 12 MHz would reduce wire time (20 MHz would be about
1.59 ms), but it introduces chain-length, signal-integrity, and hardware
validation risk. It is a contingency, not a first-line fix.

**Assessment:** asynchronous completion is P0; encoder cleanup is later.

### 4. Commands, input snapshots, and audio controls

The frame step drains an already-queued command stream without blocking,
updates fade state, snapshots input under a short lock, and updates a small
amount of smoothed audio state. Animation dispatch occurs once per active
animation. Expiry/count operations scan 32 animation slots.

No dynamic allocation occurs on the render path. These operations are bounded
and small compared with thousands of pixel operations. The 32-slot scans and a
per-frame active-animation gauge could be tightened, but only after measured
hot paths are resolved.

**Assessment:** not a priority.

### 5. Layer maintenance, scratch, and composition

The current master starts five layers enabled: FFT, Effect, TransientEffect,
Debug, and UI. Empty enabled layers are still cleared/decayed and composed over
all 960 owned pixels. Each active animation also clears a shared 960-pixel
scratch and blends the entire scratch into its destination layer, regardless of
how sparsely it drew.

Exact full-span pass counts for common cases are:

| Scenario | Begin-layer passes | Scratch clear/blend | Output clear | Compose passes | Total 960-pixel spans |
|---|---:|---:|---:|---:|---:|
| One FFT animation | 5 | 2 | 1 | 5 | **13** |
| FFT-to-FFT crossfade | 6 | 4 | 1 | 6 | **17** |

The crossfade therefore adds about 31% more fixed full-span work before its
second dense animation's own math is counted. Each additional transient adds
two more spans for scratch clear/blend; its layer was already enabled.

`Compositor::blend()` currently calls opacity scaling and switches on the blend
operation for each pixel. Target disassembly confirms that the blend-op switch
survives inside the generated pixel loop. Both values are invariant for the
whole span. A safe specialization can dispatch once per `(blendOp, opacity)`
and provide an exact `opacity == 255` path that avoids pointless scaling.

Tracking whether a layer contains any nonzero value could skip clear/decay and
compose passes for the usually empty Effect, TransientEffect, Debug, or UI
layers. This needs exact content bookkeeping: persistent trails must continue
decaying until truly black, multiple animations may share a layer, and a layer
can become empty during decay. Track a maximum/nonzero summary while writing or
scanning; do not infer emptiness from animation count alone.

Dirty rectangles or sparse-index composition are not recommended initially.
They complicate persistent decay, multi-animation blend order, and stale-pixel
clearing for a workload that is often dense.

**Assessment:** compositor specialization is a low-risk CPU win; empty-layer
tracking is promising but has more state risk.

### 6. Animation evaluation and logical ownership

Eleven animations directly use `forEachLogicalPixel()`:

`BreathingRings`, `Cymatic`, `Lighthouse`, `OrbitRing`, `PolarLattice`,
`RadialCurtain`, `Shutter`, `SpectralIris`, `SpectralWeave`, `StainedCells`, and
`Vortex`.

`Bolt`, `CenterWave`, `SineWave`, `Sinelon`, and `Starburst` also contain dense
topology loops. In total, 16 animation families can do broad logical work.
Sparse/bounded animations are `DiagnosticFill`, `OrbitSparks`, `RadialGauge`,
`SpokeSweep`, and `WheelIndicator`.

For a split renderer, `forEachLogicalPixel()` constructs a `FieldPoint` and
invokes the animation for all 1,920 logical points. Only when the animation
calls `Canvas::pixel()` does the logical-to-local map reject the other GPU's
half. Thus each GPU does expensive work for 960 pixels it can never output.

`FieldPoint` construction also recomputes topology-invariant data each pass:
spoke/radial normalization, angle, strip/physical radius, and approximate
Cartesian coordinates. Inspection of the release ELF shows these calculations
remain in the generated `StainedCells::render()` path; the compiler cannot
discard unused fields across the callback boundary. Ownership rejection occurs
after its geometry and six-seed distance work.

With the default six StainedCells seeds, each GPU currently performs at least:

```text
1,920 points × 6 seeds = 11,520 seed-distance comparisons/frame
```

Ownership-first iteration reduces that to 5,760 comparisons before considering
coordinate caching. That explains why StainedCells alone remains near the
budget in the supplied trace.

No current animation samples the previously rendered local frame through
`Canvas::sample`; current animations draw pixels/primitives. Therefore
ownership-first iteration can preserve visual output exactly, provided it is
applied only to local-frame animation paths.

Rotation is the important correctness constraint. The logical-to-local map
applies the configured rotation offset, so a fixed assumption such as “GPU0 is
spokes 0–15” is unsafe. The engine should rebuild an owned-logical index or
owned `FieldPoint` list whenever `_rebuildLogicalToLocalMap()` runs, or the new
iterator should check ownership before constructing the field point.

A staged implementation minimizes risk:

1. add an ownership-first iterator and migrate the 11 shared field-loop users;
2. migrate the five manual dense loops with per-animation output equivalence
   tests;
3. after measuring, cache invariant `FieldPoint` data in flash or an owned list
   rebuilt when geometry/rotation mapping changes.

**Assessment:** this is the largest confirmed CPU-cycle reduction, with a
broader code surface than compositor specialization.

### 7. Memory, allocation, diagnostics, and logging

The `LedDisplay` object occupies about 64,176 bytes of internal BSS in the
release ELF. Hot HSV/layer/scratch data are fixed arrays in internal memory;
the encoded buffer must remain DMA-capable. The supplied snapshots show 61.8%
of InternalData free and stable. Moving hot render buffers to external RAM
would likely reduce performance and is unnecessary.

All 21 current animation traits set `requiresFullFrame=false`; the full-frame
scratch is therefore not a current CPU cost. It may be reclaimable memory, but
removing it will not fix timing.

Existing metrics are useful and already split the major output stages. Metric
group `ledDisp` exposes `rndMax`, `showMax`, `encMax`, `spiQMax`, `spiWMax`,
`stepMax`, `slow`, `miss`, `repeat`, and `active`. They are lifetime maxima,
not distributions. Reset/reboot between A/B cases, and use temporary stage or
per-animation timing to obtain typical and p95 values.

Slow-frame logging is throttled and occurs after an overrun is detected. It can
slightly worsen an already-slow frame but is not the cause of the steady load.

**Assessment:** retain internal fixed buffers and diagnostics; improve
measurement granularity in a profiling build.

## Ranked wins

| Priority | Change | Expected effect | Risk and notes |
|---|---|---|---|
| **P0** | Return from `show()` after a successful DMA queue; reap at next `show()` | Recovers about **2.65 ms/frame of critical-path wall time** at 12 MHz | Low. Existing buffer ownership and reap/deinit paths support one in-flight transfer. This is not a CPU-cycle reduction. |
| **P1a** | Dispatch compositor once per span by blend op and opacity; exact full-opacity fast path | Reduces branches/scaling across 13 baseline or 17 crossfade full spans | Low if exhaustively equivalence-tested. Quickest true CPU change. |
| **P1b** | Iterate owned logical pixels before field/animation math | Nearly **50% less dense animation math** per GPU; StainedCells comparisons fall 11,520→5,760 | Medium-low. Must respect rotation and preserve full-output stitch equivalence. Largest true CPU win. |
| **P1c** | Cache topology-invariant `FieldPoint` values for owned pixels | Removes repeated angle/radius/Cartesian approximation work | Medium. Measure flash/cache/internal-RAM tradeoffs after P1b. |
| **P2a** | Track empty layers and skip their begin/compose spans | Can remove several of the 13 baseline passes when optional layers are black | Medium. Persistent decay and shared-layer semantics require exact bookkeeping. |
| **P2b** | Clear only SK9822 framing bytes; hoist wire-order dispatch | Saves about 3.84 KB of redundant stores/frame plus per-pixel selection | Low, but modest benefit. |
| **P3** | Cache or update expensive animation state less often, starting with StainedCells | Potentially large animation-specific saving | Changes temporal behavior unless carefully interpolated; use only if structural work is insufficient. |

Raising SPI frequency, lowering FPS, limiting simultaneous layers, or reducing
StainedCells seed count are fallback knobs. They change hardware margin or
visible behavior and should follow, not precede, the exact-output improvements.

## Recommended implementation sequence

### Phase 1: recover deadline margin

Implement one-in-flight asynchronous SPI presentation only. Keep the leading
reap, startup behavior, timeout handling, and deinit reap. This isolates the
highest-confidence change and should stop deadline misses without touching
rendered pixels.

On target, reboot before each case and capture `ledDisp` metrics for:

1. steady SpectralIris;
2. steady StainedCells;
3. the real 10-second SpectralIris→StainedCells crossfade;
4. each steady case with CenterWave transient bursts.

Also verify with a logic analyzer that each strobe launches one complete
3,968-byte transfer, transactions never overlap, and GPU0/GPU1 launch remains
synchronized.

### Phase 2: take the safest CPU cycles

Add exhaustive compositor equivalence coverage over every blend operation,
representative opacities (`0`, `1`, normal fade values, `254`, `255`), and edge
HSV values. Then specialize the span loops and measure `rndMax`/stage timings.

### Phase 3: remove discarded animation math

Add an ownership-first field iterator, with nonzero-rotation cases in the
stitch regression. Migrate animations in small groups and require bit-identical
full output reconstructed from GPU0+GPU1. Profile before deciding whether a
precomputed owned `FieldPoint` list is worth its memory/cache footprint.

### Phase 4: remove empty fixed passes if still necessary

Instrument begin-layer, animation body, scratch blend, and final compose time.
Only then add exact layer-content tracking or animation-specific work.

## Acceptance criteria

- `slow` and `miss` do not increase during long steady, crossfade, and transient
  scenarios; preferably they remain zero after startup.
- `stepMax` stays below 8 ms with meaningful jitter margin. A p99 below 7 ms is
  a practical initial target, subject to measured system jitter.
- GPU0+GPU1 output remains bit-identical to the full dense reference for every
  fixture and for nonzero rotation after ownership changes.
- SPI analyzer traces show one non-overlapping, complete transaction per
  presented frame with stable inter-GPU launch alignment.
- Transfer timeout, stop, and deinitialization paths cannot reuse or release an
  in-flight encoded buffer.

## Verification performed during this review

- `bin/build -e gpu0`: passed. RAM 64.5% (211,284 / 327,680 bytes), flash 54.3%.
- `bin/build -e gpu1`: passed. RAM 64.6% (211,604 / 327,680 bytes), flash 54.6%.
- `bin/test-led-display`: passed across all four configured logic-test profiles.
- `bin/test-led-render-stitch`: passed all 23 dense full/GPU0/GPU1 fixtures,
  including SpectralIris, StainedCells, CenterWave, and the other dense/sparse
  animation families.
- Release-ELF inspection confirmed internal fixed-buffer placement, retained
  per-point FieldPoint work in StainedCells, and the per-pixel compositor blend
  dispatch described above.
- Live `/metrics` capture was attempted, but the expected GPU USB device was not
  attached. The supplied log is sufficient to identify serialization; target
  before/after metrics are still required to quantify implementation results.

## Scope note

This document is an analysis and implementation recommendation. No rendering or
firmware behavior was changed as part of the review.
