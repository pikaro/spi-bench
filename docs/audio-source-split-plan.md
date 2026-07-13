# Audio Source Split Plan

## Goal

Split audio input ownership out of the current `Audio` component so audio
sources can be created and used independently of FFT analysis. Rename the
remaining FFT-focused component to `AudioFft`.

This is a mechanical component-boundary change, not a new audio feature. Keep
the existing PCM stream model and audio-tools stream surface intact.

The main proof point is that an audio source can be named, included, and
instantiated independently from the FFT setup while the existing media FFT path
continues to compile.

## Implementation Status

Implemented in this branch as `include/AudioSource/` and `include/AudioFft/`.
`bin/build -e media` passes with the normal I2S source selection. A temporary
`media-btstack` validation build with `BtstackA2DPSource` selected compiled the
media source root but failed at final link because the ESP32 internal DRAM
budget overflowed by 63,800 bytes. The largest contributors were the existing
media core/pubsub/SPI static objects plus the ESP32 Bluetooth controller DRAM
reserve, not the source/FFT split itself.

## Prior Shape

- `include/Audio/` owned both source devices and FFT processing.
- Source classes already share one source contract through
    `Audio/detail/Sources/IAudioSource.hpp`.
- Every current source exposes `Platform::AudioStream`, which is the
    audio-tools `AudioStream` type in the ESP32 platform layer.
- `FftAnalyzer` depends on `IAudioSource`, not on a concrete I2S source.
- `src/media/config.hpp` currently selects a source and configures FFT in one
    place, which makes source selection look FFT-owned even though it is not.

## Target Shape

- `include/AudioSource/`
    - Owns source interfaces, source configs, source status types, concrete
        source implementations, PCM ring stream, and source platform glue.
    - Exports source classes through `AudioSource/Facade.hpp`.
- `include/AudioFft/`
    - Owns FFT analyzer, FFT backend, FFT display, magnitude cache, peak
        detector, tempo tracker, FFT wire payloads, FFT commands, and FFT
        metrics.
    - Depends on `AudioSource::IAudioSource`.
- `src/media/`
    - Instantiates one `AudioSource` source.
    - Wires that source into `AudioFft::FftAnalyzer`.
    - Keeps source choice and FFT config separate in naming and includes.

The source component should be usable by a future node that only needs an I2S
audio source and does not include `AudioFft` at all.

## Move Scope

Move to `AudioSource`:

- `IAudioSource`
- `I2SSource`
- `WavSource`
- `A2DPSource`
- `BtstackA2DPSource`
- `PcmRingStream`
- source config types from `Audio/Interfaces/SourceConfig.hpp`
- source-relevant types from `Audio/Interfaces/Types.hpp`, including
    `AudioInfo`, I2S enums, `I2SPins`, and `I2SSourceStatus`
- I2S/audio stream platform glue from `Audio/detail/platform/PlatformESP32.hpp`

Keep or move to `AudioFft`:

- `FftAnalyzer`
- `FftBackend`
- `FftDisplay`
- `MagnitudeCache`
- `PeakDetector`
- `TempoTracker`
- FFT analyzer, display, peak, tempo, and wire config/types
- FFT backend platform glue from `Audio/detail/platform/PlatformESP32.hpp`

Do not add new source contract fields during this split. Keep the current
contract based on `active()`, `audioInfo()`, `ready()`, readiness polling,
read-result observation, source name, and `stream()`.

## Rename Strategy

1. Create `include/AudioSource/` with the source-side files and namespaces.
2. Create `include/AudioFft/` with the FFT-side files and namespaces.
3. Split `Audio/detail/platform/PlatformESP32.hpp` into source and FFT platform
    glue before updating broad includes. This keeps `AudioSource` from pulling
    FFT backend headers.
4. Update `FftAnalyzer` to include `AudioSource/detail/Sources/IAudioSource.hpp`
    and source `AudioInfo` types from `AudioSource`.
5. Update source classes to live under `AudioSource` paths/namespaces.
6. Update media includes and aliases so the source type is clearly an
    `AudioSource` type and the analyzer is clearly an `AudioFft` type.
7. Keep temporary compatibility includes only if needed to make the rename
    reviewable, then remove them in the same task if the diff remains small.
8. Update `docs/audio.md` or split it into source and FFT sections after the
    code move is proven by builds.

## Dependency Direction

The intended dependency direction is:

`AudioFft` -> `AudioSource` -> audio-tools stream surface

`AudioSource` must not include FFT analyzer/backend/display headers.

Use explicit namespaces:

- `Totem::AudioSource` for source contracts and source implementations.
- `Totem::AudioFft` for FFT analysis, display, peak, tempo, and wire payloads.

Avoid keeping `Totem::Audio` as the primary owner after the split. Temporary
compatibility aliases are acceptable only if they keep the implementation
reviewable and are removed before completing the split.

## Acceptance Criteria

- A source-only user can include `AudioSource/Facade.hpp`, instantiate
    `AudioSource::I2SSource`, and begin it without including `AudioFft`.
- `AudioFft::FftAnalyzer` consumes `AudioSource::IAudioSource`.
- `AudioSource` headers do not include FFT backend, analyzer, display, peak,
    tempo, or wire headers.
- The media node still wires one selected source into the FFT analyzer, but the
    source selection/config names are no longer presented as FFT-owned config.
- Existing source behavior remains unchanged.

## Validation

Compile-only validation is sufficient for this step.

Required checks:

- Build the media node with the normal I2S source selection:
    `bin/build -e media`
- Build the media node with BTstack A2DP selected:
    - Temporarily reactivate the commented `env:media-btstack` in
        `platformio.ini`.
    - Use the existing `sdkconfig.stack.media-btstack`.
    - Select `AudioSourceKind::BtstackA2DP` for that validation build.
    - Disable optional debug display if internal heap or flash pressure blocks
        the compile.
    - Run `bin/build -e media-btstack`.
    - Revert temporary source-selection and environment changes before
        finishing unless the owner explicitly asks to keep the diagnostic env
        active.

Do not use Bluedroid A2DP as the preferred Bluetooth validation target. The
Bluedroid implementation dynamically allocates heavily and is not the direction
for this split.

Useful follow-up checks if the split touches broad shared headers:

- Build `ai` if it still uses the I2S source directly.
- Build active non-audio targets only if common platform or service headers are
    changed.

## Non-Goals

- Do not add an `AudioSink` component in this task.
- Do not add new PCM metadata fields or a stronger PCM contract yet.
- Do not add runtime source switching.
- Do not make Bluedroid and BTstack coexist in one firmware.
- Do not invest in Bluedroid A2DP cleanup as part of this split.
- Do not change FFT behavior, source buffering behavior, source readiness
    behavior, or media PubSub payloads.

## Expected Risks

- The largest mechanical risk is include churn from renaming `Audio` to
    `AudioFft` while moving shared source types to `AudioSource`.
- The main conceptual risk is accidentally leaving `AudioSource` dependent on
    FFT backend headers through the platform layer.
- The main validation risk is BTstack build configuration, not runtime behavior;
    this step only proves compile-time separation.
