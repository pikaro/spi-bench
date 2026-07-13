# Audio DSP pipeline restructuring plan

This is the working plan for restructuring the media-node DSP pipeline around
tempo and beat tracking as the primary product. The scaled FFT-style band frame
remains an important output, but it is a compact exported view, not the core
reason for the pipeline to exist.

The current consumers and producers are experimental. Do not infer final
architecture from today's PubSub bridge, GPU animation, or master orchestration
code. Use the existing code only as the current implementation baseline and as
evidence for CPU, RAM, and bus constraints.

## Goals

- Make tempo tracking the central output of the audio pipeline.
- Emit explicit beat outcome events:
    - a beat was expected and confirmed by audio
    - a beat was expected and missed
    - a beat was re-acquired after a breakdown or drop
    - the tracker lost confidence and stopped making reliable beat claims
- Split tempo-domain beat events from band-local peak/accent events.
- Keep a compact `n`-band scaled frame available to consumers.
- Keep consumers independent of source details. I2S, WAV, Bluetooth, and future
  source changes must feed the same analysis/export surface.
- Improve responsiveness by decoupling FFT length from analysis hop size.
- Keep stage order and configuration fixed at compile time.
- Keep storage bounded and suitable for ESP32 classic.
- Keep wire volume under deliberate control. Higher analysis cadence does not
  imply publishing every internal frame.

## Non-goals

- Runtime plugin registration.
- Runtime-reorderable DSP graphs.
- Consumer-specific DSP branches.
- A host-only or desktop-heavy design that cannot run on ESP32 classic.
- Replacing the whole audio source selection model.
- Adding persistent diagnostic PlatformIO environments.
- Constraining the DSP event model to today's coarse effect-layer wave output.

## Constraints

- Use `constexpr` or static configuration for project-owned choices.
- Prefer fixed-size arrays and precomputed tables over dynamic allocation.
- Keep hot frame processing predictable and bounded.
- Avoid source-specific behavior after sample acquisition. Sources should
  converge into common sample and feature frames quickly.
- Keep Bluetooth-era RAM pressure in mind. Internal richness must be paid for
  with small fixed structures, not large frame histories.
- Metrics are required for new timing, drops, lock state, beat outcomes, and
  publication throttling.

## Target pipeline

The conceptual target is:

```text
Audio source
-> sample ingress
-> input conditioning
-> overlapping spectral analysis
-> perceptual band projection
-> loudness mapping and normalization
```

After loudness mapping, the pipeline branches:

```text
prepared band energy
-> export scaling
-> scaled n-band frame
-> compact PubSub export

prepared band energy
-> onset novelty extraction
-> peak/accent extraction
-> PeakEvent
-> compact PubSub export

prepared band energy + onset novelty
-> tempo and phase tracking
-> BeatEvent
-> compact PubSub export
```

This should be implemented as compile-time selected, statically owned stages.
The stages should have compatible input/output types so a stage can be replaced
or bypassed by changing config and construction code, not by changing runtime
state.

## Core data products

Introduce richer internal data products before changing wire behavior:

```cpp
struct AudioSampleBlock;
struct ConditionedSampleBlock;
struct SpectrumFrame;
struct BandEnergyFrame;
struct ScaledBandFrame;
struct OnsetFrame;
struct PeakEvent;
struct TempoState;
struct BeatEvent;
```

`ScaledBandFrame` is the consumer-facing visualization product. Its exported
band count is a build/product decision. Changes inside the pipeline must map
back to this stable exported band count.

`TempoState` and `BeatEvent` are the beat-tracking products. Beat
detection should no longer be modeled as "an onset happened now" only.

## Beat, peak, and frame semantics

Keep these concepts separate:

- `ScaledBandFrame`: continuous post-DSP spectral energy. This is the stable
  `n`-band visualization/control frame.
- `PeakEvent`: a local transient in a configured band or group. Bass, mid, and
  high peaks are useful as animation accents and tracker evidence, but they are
  not automatically musical beats.
- `BeatEvent`: an event produced by the tempo clock. It describes whether
  an expected beat was confirmed, missed, re-acquired, or no longer reliable.

The current multi-band beat detector should be treated as a naming/modeling
mistake. Multi-band onset extraction remains useful, but its direct event
surface should be peak/accent detection. The 4/4 beat tracker should output one
tempo-domain beat stream tied to BPM, phase, and confidence.

For EDM, the tempo tracker will usually be anchored mostly by kick/bass and
broadband onset evidence. It should still be allowed to use higher-band
transient energy as supporting evidence because real inputs, microphones, and
Bluetooth paths may lose or distort low-end energy. That evidence must not turn
into separate "high-band beats."

## Beat outcome semantics

The tempo tracker owns the beat clock. It estimates BPM, phase, confidence, and
expected beat windows. An onset detector only supplies evidence.

Required event kinds:

- `ExpectedHit`: a beat window was predicted and matching onset energy arrived.
- `ExpectedMiss`: a predicted beat window closed without enough confirming
  onset energy.
- `Reacquired`: the tracker regained a convincing beat after low confidence,
  silence, breakdown, or a drop re-entry.
- `Lost`: confidence fell below the threshold for reliable predictions.

Potential later event kinds:

- `Prediction`: emitted before an expected beat if consumers need lead time.
- `UnexpectedOnset`: strong onset outside the current beat window, useful for
  fills or tracker diagnostics.

The first implementation should not require consumers to react to prediction
events. Outcome events are required scope.

Peak event semantics:

- `PeakEvent` should identify the group or band range and onset strength.
- Peak events can be frequent and should be publish-rate limited or locally
  coalesced if needed.
- Peak events should not carry BPM or beat phase. If a consumer wants to align
  peaks to the beat clock, it should combine `PeakEvent` with the current
  `TempoState` or recent `BeatEvent`.

## Animation output sanity check

The current beat-to-animation mapping is useful as a coarse diagnostic view of
music reactivity. Today, peak-like audio events can create large effect-layer
waves, and several overlapping waves can dominate the whole output. This is a
valid bring-up tool, but it should not be treated as the target visual design or
as a constraint on the DSP event model.

The target direction is closer to:

- background-layer audio fields driven by the scaled band frame and beat state
- feedback warping and constrained feedback memory driven by beat phase,
  confidence, and selected band energy
- Voronoi/Worley-style cells, masks, or field seeds modulated by beat and FFT
  features
- sparse effect-layer overlays for true accents, drops, re-acquisition, or
  exceptional peak events

This means the audio pipeline should export stable, semantically clear signals,
not commands that imply a specific current animation. `BeatEvent` should
support tempo-locked background behavior. `PeakEvent` should support optional
accent overlays. `ScaledBandFrame` should support continuous field modulation.
Animation policy decides how dominant any event becomes.

## Phase 1: Split current analyzer into explicit stages

Current `FftAnalyzer` owns source copying, FFT backend setup, band reduction,
magnitude cache update, beat tracking, frame dispatch, beat dispatch, metrics,
and commands. Split this without changing behavior first.

Initial files to create or reshape:

- `include/AudioFft/Interfaces/PipelineConfig.hpp`
- `include/AudioFft/Interfaces/FeatureFrames.hpp`
- `include/AudioFft/detail/InputConditioner.hpp`
- `include/AudioFft/detail/SpectrumAnalyzer.hpp`
- `include/AudioFft/detail/BandProjector.hpp`
- `include/AudioFft/detail/BandScaler.hpp`
- `include/AudioFft/detail/OnsetExtractor.hpp`
- `include/AudioFft/detail/PeakExtractor.hpp`
- `include/AudioFft/detail/TempoTracker.hpp`
- `include/AudioFft/detail/AudioPipeline.hpp`

Implementation rules:

1. Keep `FftAnalyzer` as the compile-time owner during the transition, but break
   internal names, wire payloads, and orchestration semantics whenever the
   concepts change.
2. Move logic mechanically first: band planning, weighting, compression, cache
   scaling, and current onset/peak logic should become stage-owned code with the
   same output.
3. Keep stage state as direct members of the owning pipeline object.
4. Do not add virtual dispatch or heap-owned stage chains.
5. Rename the current multi-band beat concepts only when the split is explicit:
   group onsets become peak/accent evidence, while beat remains tempo-domain.
6. Add tests only where a host-compatible pure stage can be tested without
   PlatformIO friction. Otherwise validate with focused media builds and WAV
   fixtures.

## Phase 2: Add overlap and frame-rate control

The current 1024-sample FFT length is coupled to a 1024-sample stride. At
32 kHz this gives about 31.25 analysis frames per second. Keep FFT length and
hop size separate.

Planned defaults to evaluate:

- `fftLength = 1024`
- `analysisHop = 512` for the first low-risk improvement
- `analysisHop = 256` as the target if CPU and bus behavior hold
- exported scaled-frame publish rate capped separately from analysis rate

Implementation steps:

1. Rename config language from `stride` to `analysisHop` where practical.
2. Confirm the selected audio-tools backend supports the desired overlap
   behavior correctly.
3. Add metrics for internal analysis frames and exported frames separately.
4. Publish only the latest scaled frame at a configurable maximum rate.
5. Let beat tracking consume every internal analysis frame.

This is the main responsiveness improvement. Frequency-bin spacing stays tied
to FFT length; beat timing improves because hop size shrinks.

## Phase 3: Add input conditioning

Add a small conditioning stage before FFT input or immediately after sample
block acquisition, depending on what the audio-tools integration permits cleanly.

Required conditioning:

- DC blocker or high-pass filter
- optional fixed input gain
- clipping counter
- short-window RMS or peak estimate
- silence/noise-floor estimate

Optional conditioning:

- slow AGC for sources with unstable level
- source calibration profile for known microphone/sound-card differences

Implementation notes:

- Prefer simple one-pole filters and fixed-size state.
- Keep coefficients in config.
- Record clipping and RMS metrics so bad input wiring or source gain is visible.
- Do not let AGC hide all dynamics from onset extraction. If AGC is enabled,
  onset extraction should still have access to a stable relative novelty signal.

## Phase 4: Replace fixed FFT bands with a band projector

The current bands are hard octave-like ranges. Replace this with a projector
stage that maps FFT bins into configured perceptual/log bands.

First target:

- precomputed log/Bark-like triangular filterbank
- 12 or 16 internal analysis bands
- 8 exported scaled bands unless the wire schema is intentionally changed

Implementation steps:

1. Build a compile-time or begin-time band plan from sample rate, FFT length,
   and configured band edges.
2. Convert FFT magnitudes to power before band aggregation.
3. Project bins into internal bands using fixed weights.
4. Keep per-band center frequency for calibration and weighting.
5. Down-project internal bands to exported bands only at the export/scaling
   boundary.

Use Mel, Bark, ERB, or custom log spacing as a configurable strategy, but keep
the first implementation simple and inspectable. For this product, stable onset
and tempo features matter more than textbook-perfect psychoacoustic labels.

## Phase 5: Loudness mapping and normalization

Separate three concerns that are currently close together:

- physical/source calibration
- perceptual/loudness mapping
- display/export scaling

Recommended order:

```text
band power
-> calibration gain
-> optional perceptual weighting
-> log or dB-like compression
-> slow adaptive normalization
-> fast attack/release export smoothing
```

Tempo tracking should consume prepared band energy and onset novelty, not the
final display-smoothed `0..255` frame.

Export scaling should remain frame-driven, bounded, and low latency. The scaled
frame may use smoothing tuned for visual stability; the beat tracker should use
less-smoothed features tuned for timing.

## Phase 6: Add onset novelty extraction

Replace direct "energy crossed threshold" beat evidence with explicit onset
features. Onsets are internal evidence. Peaks and beat outcomes are the exported
event products.

Initial onset features:

- positive spectral flux over prepared internal bands
- group flux for bass, mid, high, and full-range energy
- adaptive local threshold over recent onset history
- refractory or window suppression per group

Implementation details:

1. Store only a short fixed history.
2. Use half-wave rectified deltas so falling energy does not count as onset.
3. Normalize novelty by recent local context, not by final visual scaling.
4. Preserve group-specific configuration.
5. Emit an `OnsetFrame` every analysis hop.

This stage should produce evidence for the peak extractor and tempo tracker, not
final beat events.

## Phase 7: Add peak/accent extraction

Add a peak extractor that converts onset novelty into local transient events.
This replaces the current idea of bass/mid/high "beats" with semantically
clearer accent events.

First peak groups:

- bass or kick-region peak
- mid or snare/body peak
- high or hat/noise peak
- optional broadband peak

Implementation steps:

1. Apply group-specific adaptive thresholds to `OnsetFrame`.
2. Use short refractory windows so a ringing transient does not emit repeated
   peaks.
3. Emit `PeakEvent` with group, strength, source frame, and timestamp.
4. Keep peak extraction independent from tempo confidence.
5. Let the tempo tracker consume the same onset evidence directly rather than
   consuming already-thresholded peak events only.

Peak events are useful for animation accents, diagnostics, and live
visualization. They are not proof that the 4/4 beat happened.

## Phase 8: Implement tempo and phase tracking

The tempo tracker is required scope. It should maintain one or more tempo
hypotheses and produce beat outcome events.

First viable tracker:

- primary tempo range, for example 60-180 BPM for bass
- configurable group preference for tempo evidence
- current BPM estimate
- current beat phase
- confidence score
- expected beat window width
- miss tolerance
- reacquire threshold
- lock-lost threshold

Implementation strategy:

1. Start with one primary tracker consuming kick/bass and broadband onset
   novelty.
2. Use inter-onset intervals to seed BPM candidates.
3. Predict the next beat window from BPM and phase.
4. Confirm expected beats when onset evidence arrives in-window.
5. Emit misses when the window closes without confirmation.
6. Drop confidence on misses and off-grid strong onsets.
7. Reacquire when repeated strong onsets establish a new tempo/phase after low
   confidence.
8. Keep the state machine explicit and heavily instrumented.

Later improvements:

- multiple tempo hypotheses
- half-time/double-time handling
- downbeat or phrase confidence
- multiple evidence groups with a primary fusion layer

## Phase 9: Redesign compact export

Keep export compact and decoupled from internal analysis cadence.

Scaled frame export:

- publish `n` scaled bands
- publish at a capped rate independent of analysis hop
- latest-frame semantics are acceptable for visual frames
- include sequence/timing only if the wire cost is justified

Beat outcome export:

- publish sparse beat outcome events
- include event kind, confidence, BPM, energy, sequence, and timing/phase error if
  affordable
- misses must be published even though there was no audio onset
- reacquire must be distinguishable from an ordinary hit

Peak export:

- publish peak/accent events separately from beat outcomes
- include group, energy, source frame sequence, and band range
- rate-limit or coalesce if a noisy source creates too many peaks
- do not include BPM or beat confidence in peak events

Potential wire shape:

```cpp
enum class BeatEventKind : uint8_t {
    ExpectedHit,
    ExpectedMiss,
    Reacquired,
    Lost,
};

struct BeatEvent {
    BeatEventKind kind;
    uint8_t bpm;
    uint8_t confidence;
    uint8_t energy;
    uint32_t sequence;
};

enum class PeakGroup : uint8_t {
    Bass,
    Mid,
    High,
};

struct PeakEvent {
    PeakGroup group;
    uint8_t energy;
    uint8_t lowerBand;
    uint8_t upperBand;
    uint32_t frameSequence;
};
```

The first schema break has landed with this split: current firmware publishes
`PeakEvent` for bass/mid/high accents and `BeatEvent` for first-pass
tempo-clock outcomes. Further beat-tracker fields, such as phase error, should be
added when the tracker has enough validation data to justify them. Schema changes
require regenerated wire support and multi-environment build validation.

## Phase 10: Host visualization and local fanout

The master already has UDP PubSub transport support, and the host tooling
already has a Python bridge around the C++ UDP peer. Use that path for
development visualization instead of relying on the small SSD1306 display.

Target shape:

```text
MCU PubSub over UDP
-> host C++ UDP peer
-> Python bridge
-> local socket fanout
-> independent visualization/debug scripts
```

The bridge should remain the single trusted PubSub participant from the MCU's
point of view. Local visualization scripts should not each become separate MCU
UDP peers.

Suggested local fanout behavior:

- expose a Unix-domain socket first, with localhost TCP as an optional fallback
- accept multiple local clients
- let each client request subscriptions for FFT frames, peaks, beat outcomes,
  and later tempo-state snapshots
- when the first local client subscribes to a topic, send the PubSub subscribe
  command through the UDP bridge
- when the last local client for a topic disconnects, send unsubscribe
- forward generic PubSub event JSON to each subscribed local client, including
  the envelope header and raw `payload_hex`
- drop or coalesce high-rate frame messages per client instead of allowing a
  slow visualization client to backpressure the UDP peer

Initial implementation status: the C++ UDP participant remains generic and
emits raw `payload_hex` for arbitrary PubSub topics. The Python wrapper exposes
a Unix-domain socket where local scripts can subscribe, unsubscribe, publish raw
payloads, and receive generic newline-delimited PubSub JSON. Audio-specific
FFT/peak/beat decoding belongs in the visualization script or generated
bindings, not in the UDP participant or bridge.

Recommended local protocol:

```json
{"op":"subscribe","topic":16}
{"op":"subscribe","topic":2048}
{"op":"subscribe","topic":8}
{"op":"unsubscribe","topic":16}
{"op":"publish","topic":16,"traffic_class":0,"payload_hex":"0001"}
```

Forwarded events are newline-delimited JSON. The bridge handles the PubSub
envelope and transport. Message-specific consumers decode `payload_hex` using
generated bindings or their own explicit topic dependency.

Visualization targets:

- scrolling `n`-band frame display
- peak markers by group
- beat phase display with expected-hit, expected-miss, re-acquired, and
  lock-lost markers
- BPM and confidence readout
- event-rate and dropped-client-message counters

Implementation constraints:

- Do not duplicate the binary PubSub codec in visualization scripts.
- Keep C++ responsible for UDP and binary encode/decode.
- Keep Python responsible for local fanout, decoded event models, and app-level
  dispatch.
- Keep local fanout subscriptions explicit and reference-counted.
- Treat host visualization as a diagnostic/control-plane tool, not as part of
  the timing-critical firmware loop.

## Phase 11: Validation plan

Use generated WAV fixtures before relying on live music:

- steady click track at known BPM
- kick-like pulse at known BPM
- off-grid transient noise
- tempo ramp
- breakdown with no beat, then re-entry
- half-time/double-time ambiguity
- high-band accents that must create peaks but not separate beats
- fills or risers that should not permanently move the beat phase
- silence and low-level noise
- clipped input
- music excerpts captured from expected real sources if available

Validation surfaces:

- `/peaks` for current peak extraction status
- `/tempo` for current BPM, confidence, lock state, hit/miss/reacquire/lost
  counts, and last event age
- metrics for analysis frame rate, export frame rate, CPU timing, source clips,
  onset level, peak counts, tempo confidence, hit/miss/reacquire/lost counts,
  and dropped exports
- Future GPIO beat indicator for `ExpectedHit` and separate diagnostic indication
  for `ExpectedMiss` or `Reacquired` if useful during bring-up
- host-side visualization through the UDP bridge for readable live beat, peak,
  and FFT inspection
- bounded monitor captures from `media`

Build validation:

- `bin/build -e media` after audio-only changes
- `bin/build -e master -e media -e gpu0 -e gpu1 -e io` after wire/schema or
  shared PubSub changes

Runtime validation:

- WAV source first, because it gives repeatable known-BPM input
- I2S microphone/source second, because real acoustic input exposes gain,
  noise, and room behavior
- Bluetooth source only after RAM and source behavior are checked

## Implementation sequence

1. Add internal frame/config types and stage skeletons.
2. Mechanically move current analyzer behavior into stages.
3. Build `media` and confirm scaled frames and current explicit peak export
   still operate.
4. Add analysis/export rate split and overlap.
5. Add input conditioning and metrics.
6. Add band projector and internal band frame.
7. Add onset extractor.
8. Add peak extractor and rename current multi-band beat concepts accordingly.
   Initial implementation is complete for the existing detector path.
9. Add tempo tracker and publish beat outcome events.
   Initial implementation is complete as a single bass-evidence inter-onset/phase
   tracker.
10. Add compact peak and beat outcome wire export.
    Initial implementation is complete for `PeakEvent` and `BeatEvent`.
11. Add host-side local fanout and visualization scripts.
12. Tune with WAV fixtures and live I2S captures.
13. Update `docs/audio.md` once the implemented shape is stable.

## Open decisions

- Exported scaled band count: keep 8 initially or intentionally move to another
  `n`.
- First internal band count: 12 or 16.
- First projector scale: custom log, Bark-like, Mel, or ERB.
- First hop target: 512 samples or 256 samples.
- Whether `Prediction` events are needed before outcome events.
- Whether phase error belongs in the first beat outcome wire schema.
- Peak event schema and rate limits.
- Local fanout socket protocol details and whether Unix-domain sockets are
  enough for the first host visualization tool.
- How much of the validation harness should be host-side pure C++ versus
  firmware/WAV-source driven.
