# AI audio AFE implementation plan

This is the working plan for inserting an ESP-SR Audio Front-End (AFE) into the
AI node's delayed microphone loopback. The first implementation is deliberately
local: detect a wake word, indicate recording immediately on the status LED,
then play the cleaned and normalized microphone stream through the existing
one-second delay and MAX98357 speaker output.

The processed stream must remain independent of the local speaker sink. The
next step will route the same post-wake stream to a WebSocket sink and use a
separate response stream for speaker playback.

## Scope

In scope:

- ESP-SR AFE on `env:ai`.
- The existing SPH0645 microphone and MAX98357 speaker wiring.
- The existing 32 kHz, signed 32-bit mono microphone input.
- Conversion to the 16 kHz, signed 16-bit mono format required by ESP-SR.
- Single-channel noise suppression, VAD, WakeNet, and AGC.
- A stock ESP-SR wake word for initial validation, preferably `Hi ESP`.
- Speaker output contains only delayed frames captured during a post-wake
  recording session; waiting-state microphone audio never reaches the speaker.
- The existing one-second PCM delay between the session-selected stream and
  speaker. This is required by the proven local hardware path to avoid an
  immediate acoustic feedback loop.
- A node-local `Recording` status LED state set by WakeNet and reset when VAD
  ends the utterance.
- Bounded buffers, useful metrics, and concise state-transition logging.

Out of scope:

- A custom wake-word model.
- MultiNet command recognition.
- WebSocket transport or remote ASR.
- Playback of a remote response.
- Acoustic echo cancellation (AEC). The current input has one microphone
  channel and no playback-reference channel.
- Dual-microphone processing, beamforming, or BSS.
- A general runtime-configurable DSP graph.
- Reworking the shared `AudioSource` or `AudioSink` interfaces.

## Current baseline

The AI node currently does this:

```text
SPH0645
  32 kHz / 32-bit / mono
-> ObservedMicStream
-> Pcm16DownsamplerStream
  16 kHz / 16-bit / mono
-> one-second PCM delay
-> MAX98357 I2S sink
```

This proves the microphone, format conversion, second I2S port, amplifier, and
speaker path. The new work should retain that known-good hardware setup while
inserting AFE processing and wake-word gating before the existing delay.

The one-second delay line must remain. The status LED, rather than immediate
speaker output, provides instant feedback that WakeNet opened the recording
session. The delay remains a local playback policy and must not become part of
the reusable AFE output contract.

## Assumptions

Non-blocking assumptions for the first implementation:

- Use the stock `Hi ESP` WakeNet model. Model customization is a later product
  decision.
- WakeNet runs continuously. A wake detection always opens the recording
  session regardless of the current or previous VAD state.
- VAD is not a precondition, filter, or gate for WakeNet. It is used only after
  wake-up to find the end of the command.
- WakeNet sets the `Recording` status as soon as it detects the wake word. The
  normal VAD timeout resets it immediately; debug playback may still emit audio
  that was already captured into the one-second delay.
- VAD controls utterance lifetime; it does not mute every individual silence
  frame. Once awake, downstream audio remains continuous enough for a future
  streaming ASR connection.
- If no speech follows the wake word, a bounded no-speech timeout closes the
  session. A separate maximum-session timeout prevents an indefinitely open
  session.
- Keep the existing exact 2:1 PCM adapter for the first pass. Improve its
  anti-alias filter only if listening tests or measurements show a problem.
- Keep the existing one-second, fixed-size PCM delay for the local I2S consumer.
  A future WebSocket consumer receives active-session AFE frames directly and
  does not inherit this artificial delay.
- ESP-SR may allocate its model and internal processing storage dynamically.
  Project-owned frame buffers and routing state remain fixed-capacity.

One implementation prerequisite must be resolved before coding: ESP-SR models
require a flash partition labelled `model`. The proposed solution is an
AI-specific 16 MiB partition table so other S3 nodes keep their existing layout.
Its app, model, LittleFS, secrets, and coredump sizes should be chosen after the
selected model bundle and firmware sizes are measured.

## Target pipeline

The production processing and session path is:

```text
SPH0645 I2S source
-> existing 32 kHz / 32-bit to 16 kHz / PCM16 adapter
-> fixed AFE feed frames
-> ESP-SR AFE
   - single-channel noise suppression
   - VAD
   - WakeNet
   - AGC
-> processed 16 kHz / PCM16 frame + detection metadata
-> session controller
   - WakeNet opens Recording unconditionally
   - VAD ends Recording only after wake-up
-> active-session processed-frame consumer
```

The AFE is always fed and fetched while waiting and recording. WakeNet is always
evaluated; no VAD condition may suppress or qualify a WakeNet detection. In
particular, do not implement logic equivalent to
`if (vad == Speech) handleWakeNet()` and keep ESP-SR VAD channel-trigger gating
disabled for this single-microphone path.

The current local validation adds a separate debug-only consumer:

```text
Recording ? processed PCM : zero PCM
-> existing one-second debug delay ring
-> MAX98357 I2S sink
```

The debug ring always advances at the normal audio cadence. It receives cleaned
PCM while `Recording` and zero PCM while waiting. Therefore already-captured
audio naturally continues to emerge for one second after the production
recording session ends, then becomes silent as the zeros arrive. This requires
no additional session state or tail-flush control path.

WakeNet and VAD metadata also drive an immediate side path:

```text
WakeNet detected -> set Recording status + log detection
VAD state change -> log transition
VAD timeout -> reset Recording status + log session end
```

The one-second delay applies only to local PCM playback. It does not delay the
status LED or detector logs.

## Session behavior

The production session model has only two states:

```text
WaitingForWake (Recording off)
  -- WakeNet detected, regardless of VAD --> Recording (Recording on)

Recording
  -- post-wake VAD has seen speech, then reports silence --> WaitingForWake
  -- no-speech safety timeout --> WaitingForWake
  -- maximum-session safety timeout --> WaitingForWake
```

While `WaitingForWake`:

- Feed and fetch AFE frames continuously.
- Evaluate and log WakeNet and VAD independently.
- Accept every valid WakeNet detection without checking VAD.
- Do not forward PCM to a production consumer.

On wake-word detection:

- Enter `Recording` unconditionally, even if the same fetch result reports VAD
  silence.
- Log the model and wake-word index once.
- Set the registered `Recording` status LED handle immediately.
- Reset `speechObserved` and session-local VAD transition history. The VAD state
  in the wake result may describe the wake word itself; it must not be treated
  as post-wake command speech and must never close the newly opened session.
- Start the no-speech and maximum-session deadlines.
- Mark the current and subsequent PCM frames as part of the active session.

While `Recording`:

- Forward every processed frame to the selected consumer, including short
  silence between words.
- Starting with the fetch result after the wake detection, let a VAD `Speech`
  observation arm command-end detection. This does not affect the already-open
  recording session.
- If post-wake VAD has not yet reported speech, continue recording until it does
  or the no-speech safety timeout expires.
- Once post-wake VAD has reported speech, treat its later
  `Speech -> Silence` transition after the configured minimum-noise duration as
  the normal command endpoint.
- Log and count repeated WakeNet detections, but do not restart or extend the
  current session.

On the normal VAD endpoint:

- Reset the `Recording` status LED handle immediately.
- Stop marking new PCM frames as active and return to `WaitingForWake`
  immediately.
- Log the VAD transition and session duration.
- Do not wait for local delayed playback before accepting a later wake word.

On no-speech, maximum-session, error, or shutdown exits:

- Reset the `Recording` handle on every path so an early return cannot leave the
  LED stuck on.
- Stop forwarding/marking new PCM immediately.
- Clear the debug delay only on pipeline failure or shutdown. Normal endpoints
  leave already-captured samples to emerge naturally.
- Reset AFE or WakeNet only if hardware testing shows that the library requires
  it for reliable re-arming.

No pre-wake audio is required in this phase. The debug delay stores zero PCM
while waiting, so it cannot play pre-wake microphone audio. The beginning of
the wake word may already have passed when WakeNet reports detection; the local
test should use a phrase such as
`Hi ESP, testing one two three` and validate the speech following the wake word.
A bounded pre-roll can be added later only if remote ASR proves to need it.

## Status LED integration

Register one AI-node-local status during setup through
`StatusLedService::directory()`:

```cpp
StateDef{
    .name = "Recording",
    .color = {.red = 160, .green = 80, .blue = 0},
    .kind = StateKind::Warning,
};
```

Use a distinct amber color because the directory rejects duplicate colors. The
current status LED selector reserves the informational slot for boot/core/target
readiness, so `Warning` is the smallest existing priority that visibly
overrides the green `TargetsReady` state. Real `Error` and `Critical` states
continue to take precedence over recording. Do not change the shared status LED
priority model for this feature.

The AI session controller owns the returned `StateHandle`:

- register it after `CoreSetup` has initialized the status service and before
  the audio task starts
- call `set()` on the accepted WakeNet transition
- call `reset()` on the VAD timeout and every safety/error/shutdown exit
- treat a set/reset failure as an operational error and count/log it

`StateHandle` updates are task-safe and mark the LED service dirty. The normal
`CoreSetup::work()` pass performs the physical update, so detection feedback is
not held behind the one-second PCM delay.

## ESP-SR integration

### Dependency and models

- Add `espressif/esp-sr` through the ESP-IDF component manager and pin an exact
  ESP-SR 2.x version that is verified against the repository's ESP-IDF version.
- Resolve the dependency through the existing per-environment
  component-manager layout and verify the generated AI lock selects the pinned
  version. Build-local lock files remain ignored, matching current practice.
- Scope ESP-SR component discovery/linking to `env:ai` through the existing
  CMake component-selection pattern. A project-owned `ENABLE_ESP_SR` switch is
  appropriate here because it controls SDK component wiring, not ordinary
  application behavior.
- Select only the first-pass NS, VADNet, and WakeNet model set in
  `sdkconfig.stack.ai`.
- Add an AI-specific partition table with a `model` data partition and point
  only `env:ai` at it.
- Ensure normal full upload flashes the generated model image as well as the
  application. Document a model-only or app-only iteration command only if the
  PlatformIO integration needs one.
- Verify the ESP-SR license and selected model redistribution terms before the
  dependency becomes part of a distributable firmware image.

### AFE configuration

Start from `afe_config_init("M", models, AFE_TYPE_SR, ...)`, then set the
first-pass policy explicitly:

- one microphone channel, no reference channel
- noise suppression enabled
- VADNet enabled
- WakeNet enabled with the selected stock model
- VAD channel-trigger gating disabled
- no application-side VAD check around WakeNet result handling
- AGC enabled
- AEC and multi-microphone speech enhancement disabled
- PSRAM-favoring allocation on the AI node
- explicit AFE core, priority, and ring-buffer sizing
- conservative AGC target/compression and unity linear output gain initially

Do not hard-code those choices in `AfeProcessor` or the ESP32 platform wrapper.
They must be translated from validated project configuration during `begin()`
so repeated microphone, room, and gain tuning does not require editing the
processing implementation.

Call the ESP-SR configuration checker and print the resulting pipeline once at
startup. Do not assume that every requested module remains enabled after the
library resolves configuration conflicts.

The microphone's 32-bit container alignment must be measured before tuning
gain. If the current right shift throws away valid SPH0645 signal bits, fix the
PCM conversion first. Do not compensate for incorrect sample alignment by
stacking large AFE linear gain on top of AGC.

Tune AGC from measured input level, processed RMS/peak, and clipping counts.
Avoid adding a second custom normalizer or limiter unless the AFE output proves
insufficient; duplicate gain stages make wake sensitivity and output volume
harder to reason about.

### Feed and fetch behavior

- Query the AFE feed/fetch chunk sizes after creation.
- Validate them against compile-time maximum frame capacities before starting
  audio.
- Accumulate converted PCM until one complete feed frame is available. Never
  pass a short frame to `feed()`.
- Use one statically allocated AI audio pump task to read the source, feed AFE,
  fetch ready results with a bounded/non-blocking wait, update session state,
  and invoke the current frame consumer.
- Process each valid fetch result in this order:
    1. handle WakeNet detection without consulting VAD
    2. update/log VAD state
    3. only if the session was already recording before this fetch result,
       update endpoint tracking from VAD; the result that opened the session
       cannot arm or end it
    4. select processed PCM or zeros for the debug delay from the resulting
       recording state
- Consume or copy the fetched frame before the next fetch because the result
  storage is owned by ESP-SR.
- Treat expected no-result polls as normal. Count and log actual feed/fetch
  failures, invalid result codes, source starvation, and sink short writes.
- Keep source reads bounded so the audio task and watchdog remain predictable.

Do not add another project-owned audio queue for the first pass. A fetched frame
should be consumed synchronously by the local sink. If the sink cannot accept a
whole frame, count the drop and continue rather than blocking the AFE pipeline
indefinitely.

## Component shape

Add a small ESP-SR-facing component rather than embedding the C API directly in
`src/ai/main.cpp`:

```text
include/AudioAfe/
  Facade.hpp
  Interfaces/Config.hpp
  Interfaces/Types.hpp
  detail/AfeProcessor.hpp
  detail/Metrics.hpp
  detail/PlatformSelect.hpp
  detail/platform/PlatformESP32.hpp
```

`AudioAfe` should own:

- ESP-SR model/config/handle/data lifecycle.
- Fixed feed-frame accumulation.
- Feed/fetch calls and validation.
- Conversion of ESP-SR result fields into a small project-owned result view.
- AFE metrics.

The AI application should own:

- The existing microphone source and PCM16 adapter.
- The WakeNet-started, VAD-ended session state and its safety timeouts.
- The registered `Recording` status LED handle.
- The current processed-frame consumer.
- A small delayed-playback consumer that retains the existing fixed one-second
  sample buffer and substitutes zeros while no session is recording. This
  consumer is debug-only and is not part of the production session model.
- The MAX98357 sink used for local validation.

Keep the consumer boundary synchronous and allocation-free, for example a
callback receiving a non-owning `ProcessedFrameView` containing PCM samples,
VAD state, WakeNet state, wake-word index, input volume, and timestamp. The
consumer must not retain the view.

This boundary is the important seam for the next phase: replace the local
delayed-I2S consumer with the existing WebSocket sink without changing
microphone capture, AFE processing, or session gating.

## Configuration and tuning

The first implementation is expected to need repeated tuning. All ordinary AFE
and session parameters must therefore be represented by small validated config
structs, with the active `constexpr` values assembled in `src/ai/config.hpp`.
No detector threshold, VAD duration, gain value, task placement, or session
timeout should be buried in `AfeProcessor`.

Suggested configuration shape:

```cpp
struct NoiseSuppressionConfig;
struct VadConfig;
struct WakeNetConfig;
struct AgcConfig;
struct AfeTaskConfig;

struct AudioAfeConfig {
    NoiseSuppressionConfig noiseSuppression;
    VadConfig vad;
    WakeNetConfig wakeNet;
    AgcConfig agc;
    AfeTaskConfig task;
    // Memory policy and bounded feed/fetch capacities.
};

struct WakeSessionConfig {
    uint32_t noSpeechTimeoutMs;
    uint32_t maximumSessionMs;
};

struct DelayedPlaybackConfig {
    uint32_t delayMs;
};
```

Expose at least these tuning fields:

- noise suppression enabled state, implementation mode, and selected model
- VAD enabled state, model/mode, minimum speech duration, minimum noise/silence
  duration, VAD look-back delay, and applicable playback/channel-trigger flags
- WakeNet enabled state, selected compiled model, detection mode, and threshold
- AGC enabled state, mode, target level, compression gain, and AFE linear gain
- AFE low-cost/high-performance mode, PSRAM allocation policy, internal ring
  depth, task core, priority, and static stack size
- maximum feed/fetch frame capacities
- no-speech and maximum-session safety timeouts
- local playback delay, initially the existing 1000 ms
- `Recording` status definition, including its distinct color

Use ESP-SR's configured VAD minimum-noise duration as the normal post-speech
timeout. When AFE reports the resulting `Speech -> Silence` transition, reset
the recording status and close the production session immediately. Do not add a
second application hangover timer initially; add one later only if hardware
traces show that the AFE transition is not a sufficient endpoint.

Validate that the single-microphone configuration leaves VAD channel-trigger
gating disabled. VAD tuning must be able to change command endpoint behavior
without changing whether WakeNet runs or whether a wake detection is accepted.

Each config type should provide `validate()` and reject invalid combinations
before models, tasks, or I2S processing start. Validation should cover the
documented ESP-SR ranges, model presence, task/core values, frame capacities,
nonzero safety timeouts, and conversion from `delayMs` to an exact bounded
sample count.

The platform wrapper should translate these structs into `afe_config_t`, run
ESP-SR's own config checker, and log both the requested high-level settings and
the effective pipeline once at startup. This makes every tuning build
reproducible.

Use `sdkconfig.stack.ai` only to compile the selected ESP-SR model assets and
SDK-owned features. Keep tuning values in the C++ config structs, not Kconfig or
new build flags. The first pass does not require runtime mutation commands;
edit the node config, rebuild, flash, and record accepted values in
`docs/audio.md`.

## Metrics and logging

Add bounded metrics sufficient to tune and validate the pipeline:

- source bytes and short/empty reads
- feed and fetch frames
- feed/fetch failures
- AFE ring-buffer pressure or reported free percentage
- WakeNet detections
- repeated WakeNet detections observed during an active recording
- VAD speech/silence transitions
- sessions opened and closed by reason
- active-session duration
- processed bytes forwarded and discarded
- delayed debug playback samples and waiting-state zero-fill samples
- sink short writes/dropped frames
- recording status set/reset failures
- input volume reported by AFE
- processed-frame peak/RMS and clipped-sample count
- audio task runtime and high-water stack usage

Prewarm metrics during `begin()` before the audio task starts, matching the
project metrics contract.

Log only high-signal events by default:

- selected models and final AFE pipeline at startup
- every accepted WakeNet detection, including model and wake-word index
- every VAD `Silence -> Speech` and `Speech -> Silence` state change during an
  active session
- transition from waiting to recording and the corresponding `Recording`
  status set
- transition from recording to waiting, including the VAD/safety close reason
  and `Recording` status reset
- unexpected source, AFE, or sink failures

Keep logging ownership unambiguous: `AudioAfe` logs each raw WakeNet detection
and can log every raw VAD state change at debug level regardless of session
state. The AI session controller logs post-wake VAD endpoint activity, derived
session changes, and `Recording` status transitions at the normal operational
level. Do not emit the same detector event twice at the same level.

Do not log unchanged per-frame WakeNet or VAD results.

## Likely files

- `include/AudioAfe/Facade.hpp`
- `include/AudioAfe/Interfaces/Config.hpp`
- `include/AudioAfe/Interfaces/Types.hpp`
- `include/AudioAfe/detail/AfeProcessor.hpp`
- `include/AudioAfe/detail/Metrics.hpp`
- `include/AudioAfe/detail/PlatformSelect.hpp`
- `include/AudioAfe/detail/platform/PlatformESP32.hpp`
- `src/ai/main.cpp`
- `src/ai/config.hpp`
- `src/ai/pcm16_downsampler.hpp` only if small adapter changes are required
- `src/CMakeLists.txt`
- `src/idf_component.yml` or a narrowly scoped local component manifest
- `CMakeLists.txt`
- `platformio.ini`
- `sdkconfig.stack.ai`
- a new AI-specific partition CSV under `partitions/`
- `docs/audio.md` after the runtime behavior is implemented
- `docs/commands.md` if implementation adds an AFE status command

## Implementation phases

### Phase 1: dependency and model image

1. Pin ESP-SR and resolve it in `env:ai`.
2. Select one NS model, VADNet, and the stock `Hi ESP` WakeNet model.
3. Add the AI-specific model partition and verify the model binary is generated.
4. Build and flash a minimal AFE initialization probe.
5. Confirm the final pipeline, firmware size, model size, internal RAM, and
   PSRAM usage before adding routing behavior.

### Phase 2: AFE processor

1. Add the `AudioAfe` component and lifecycle handling.
2. Feed the existing converted PCM16 stream in complete AFE chunks.
3. Fetch processed frames and detection metadata without speaker output.
4. Populate the initial NS, VAD, WakeNet, AGC, and task config structs from
   `src/ai/config.hpp` and log their effective ESP-SR pipeline.
5. Add metrics and validate continuous operation, WakeNet detections, VAD
   transitions, ring-buffer headroom, and task runtime.

### Phase 3: wake-gated local playback

1. Add the small session state machine in the AI application.
2. Register the amber `Recording` status and connect WakeNet/VAD transitions to
   its handle and the activity logs.
3. Split `DelayedMaxLoopback` into the AFE input path and a delayed-playback
   debug consumer, preserving its fixed one-second sample delay.
4. Feed cleaned PCM into the delay while recording and zeros while waiting,
   without adding a production playback-tail state or a second buffer.
5. Tune VAD, WakeNet, AGC, NS, and session values only through their config
   structs.
6. Verify repeated wake/listen/end/re-arm cycles without rebooting, including a
   new wake while the previous debug playback tail is still emerging.

### Phase 4: hardening and documentation

1. Run an extended hardware test with WiFi enabled.
2. Confirm no task watchdog events, AFE overruns, sink drops, or growing heap
   loss.
3. Record accepted model versions, memory use, final gain/VAD values, and
   hardware observations in `docs/audio.md`.
4. Keep the WebSocket consumer change explicitly out of this implementation.

## Validation

Build validation:

```text
bin/build -e ai
```

Hardware acceptance checks:

1. Boot the AI node and confirm the logged AFE pipeline contains the intended
   NS, VAD, WakeNet, and AGC stages.
2. Speak near the microphone without the wake word until VAD reports speech and
   silence. The speaker must remain silent and the status LED must remain at its
   normal ready state, proving VAD activity cannot open a session.
3. Say `Hi ESP, testing one two three` at several levels and around VAD state
   changes. Every valid WakeNet detection must open the session and turn the
   status LED amber immediately, regardless of VAD.
4. Confirm that the cleaned speech begins at the speaker after the configured
   one-second delay, not immediately on detection.
5. Stop speaking. VAD must log its speech-to-silence transition and reset the
   amber `Recording` status after its configured silence duration.
6. Confirm already-captured delayed frames finish playing without delaying
   session re-arming, while later waiting-state zero samples keep the speaker
   silent.
7. Repeat the wake phrase several times without rebooting and verify the node
   returns to green `TargetsReady` between sessions.
8. Test quiet and loud speech at several distances. Quiet speech should be
   raised usefully; loud speech should not show persistent clipping.
9. Test steady background noise. Noise should be reduced and should not cause
   frequent false wakes or permanently open sessions.
10. Inspect metrics for source starvation, feed/fetch failures, ring-buffer
    pressure, status update failures, dropped sink frames, clipping, task
    runtime, and stack headroom.
11. Leave the node running with WiFi enabled long enough to catch task
    starvation or memory loss.

Local speaker playback can be picked up by the microphone. Preserve the proven
one-second delay because removing it creates an immediate feedback loop on this
hardware. The delay is not AEC and should not be treated as an echo-cancellation
result. If true simultaneous capture and speaker playback becomes a product
requirement, add the exact playback PCM as an `R` reference channel and plan AEC
as a separate phase.

## Follow-on WebSocket phase

The next phase should keep the production session behavior unchanged:

```text
microphone -> PCM adapter -> AFE
-> WakeNet opens Recording unconditionally
-> VAD ends the post-wake command
-> WebSocket sink while Recording

WebSocket response -> response PCM source/decoder -> MAX98357 sink
```

The one-second delay belongs to the local diagnostic speaker consumer and is
replaced with it. It is not a production state and does not delay the
outbound WebSocket stream. The immediate `Recording` status and independent
WakeNet/VAD logs remain unchanged.

That phase will need explicit connection ownership, packetization/backpressure,
remote audio framing, and response playback behavior. None of those concerns
should leak into `AudioAfe` or the first local validation implementation.

## ESP-SR references

- [ESP-SR Audio Front-End framework](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/audio_front_end/README.html)
- [WakeNet model documentation](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/README.html)
- [ESP-SR model selection and partition loading](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/flash_model/README.html)
- [ESP32-S3 ESP-SR resource benchmarks](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/benchmark/README.html)
- [ESP-SR component registry](https://components.espressif.com/components/espressif/esp-sr)
