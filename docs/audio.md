# Audio

`include/Audio/` owns the media-node audio input and FFT analysis path.

## Component Shape

- `Audio/Facade.hpp` exports the public audio classes.
- `Audio/Interfaces/Types.hpp` contains value-type configuration and callback
    payloads for I2S input, FFT frames, magnitude scaling, and beat events.
- `Audio/detail/Sources/` owns selectable audio inputs. `AudioSource` starts
    exactly one configured source and exposes it through `IAudioSource`.
- `Audio/detail/FftAnalyzer.hpp` feeds the source into arduino-audio-tools'
    stream/sink FFT path, calculates fixed FFT bands, updates the magnitude
    cache, and emits frame/beat callbacks.
- `Audio/detail/FftBackend.hpp` selects the active arduino-audio-tools FFT
    implementation and exposes it to `FftAnalyzer` through the common
    `AudioFFTBase` surface.
- `Audio/detail/FftDisplay.hpp` is an optional media-node debug visualizer for
    a 128x32 SSD1306 I2C display. It subscribes to FFT frames and beats but
    flushes the display from its own task.
- `Audio/detail/platform/PlatformESP32.hpp` is the component-owned ESP32
    platform layer for arduino-audio-tools I2S/FFT types and ESP32-A2DP.

The current design supports one simultaneous audio source. That keeps ownership
and runtime scheduling simple while still allowing source selection through
`AudioSourceConfig::kind`.

## Audio Sources

`AudioSourceConfig::kind` selects the active source:

- `AudioSourceKind::I2S` uses `I2SSource` for live microphone or sound-card
    input.
- `AudioSourceKind::WavFile` uses `WavSource` to stream a PCM RIFF/WAVE file
    from LittleFS without buffering the whole file in RAM. The default path is
    `/test.wav`.
- `AudioSourceKind::A2DP` uses `A2DPSource` as a Bluetooth A2DP sink through
    `pschatzmann/ESP32-A2DP`. Output is disabled; decoded PCM is copied from
    the library callback into a fixed ring buffer that the FFT task reads.
    The analyzer-facing output defaults to mono: decoded 16-bit stereo PCM from
    Bluedroid is downmixed before it enters the FFT ring. The ring is
    intentionally small, currently 3 KB, because classic Bluetooth leaves
    limited internal DRAM on the original ESP32 and the buffer only has to
    bridge callback bursts to the FFT task.

Only the selected source is started at runtime. Config for the selected source
must be present in `AudioSourceConfig`; inactive configs may be omitted.

`bin/wavgen.py` writes short LittleFS-friendly fixtures to
`data/media/littlefs/test.wav` by default. Upload the filesystem image before
testing `WavFile` so the mounted LittleFS contains the sample.

Enabling `A2DPSource` requires the ESP-IDF Bluetooth stack. On the original
4 MiB ESP32 media board this has a large flash-size cost, so A2DP should be
treated as a diagnostic input until the linked firmware size is validated.

## I2S Devices

`I2SSourceConfig::device` selects a preset from the component device library.
The first preset is `I2SDevicePreset::LegacySoundCard`, copied from
`legacy/include/Pins.hh`:

- bit clock: GPIO10
- word select: GPIO21
- data input: GPIO20

`I2SDevicePreset::Custom` uses `I2SSourceConfig::customLink` for hardware that
does not match a preset. `I2SLinkConfig::hostClockRole` is always from the ESP32
host perspective: `ProvidesClock` means the ESP32 drives BCLK/WS, while
`ConsumesExternalClock` means the ESP32 waits for externally generated BCLK/WS.
The legacy sound card preset consumes external clocks; the SPH0645 preset
provides clocks because the microphone is the I2S slave. The SPH0645 preset is
currently 32 kHz, 32-bit, Philips-format mono input. The microphone can support
higher rates, but 32 kHz proved the better latency and CPU-load point for the
media node while retaining the useful FFT range.

`I2SSource` starts in offline/probe mode. The source performs short bounded I2S
reads at `I2SSourceReadinessConfig::probeIntervalMs`; once bytes arrive, the FFT
task is allowed to copy from audio-tools. If a running source stops producing
data, the source returns to probe mode and the FFT task skips audio copy work
until data appears again. This avoids blocking the managed `AudioFft` task on an
unclocked I2S peripheral when no external sound card is connected.
Read timeouts are expected while the source is offline and are logged by this
component at a low periodic rate with the configured pins and I2S mode instead
of surfacing audio-tools' raw `I2SDriverESP32V1::readBytes` trace.

The current `env:media` board is `custom_esp32_nodemcu` through
`platformio.ini`. The legacy preset pins are not a safe classic ESP32 NodeMCU
pinout, so hardware validation still needs a board decision or a custom device
config before unattended runtime use.

## FFT Notes

FFT band magnitudes are reduced exactly once per band. The raw reduced value is
kept as `FftBandValue::magnitude`; `FftAnalyzerConfig::signalPipeline` prepares
the separate `weightedMagnitude` value used by the magnitude cache and beat
tracker. This avoids the legacy double-averaging bug where band sums were
averaged once in the callback and then divided by bin count again during
scaling.

`FftAnalyzerConfig::backend` selects the FFT library. `EspressifFft` is the
default because it uses Espressif's DSP FFT implementation through
`AudioEspressifFFT.h`; `RealFft` remains available for comparison against
arduino-audio-tools' portable real FFT path. Both backends feed the same
`AudioFFTBase` callback into the analyzer, so band reduction, scaling, beat
tracking, display output, and metrics stay backend-independent. The ESP-DSP
component dependency is declared in `src/idf_component.yml`.

The analyzer defaults are conservative public API defaults. The active media
node config in `src/media/config.hpp` is tuned for low-latency visualization
without monopolizing the SPI-facing core:

- The SPH0645 preset runs at 32 kHz. The analyzer uses
    1024-sample FFT frames and stride, which keeps the same roughly 32 ms frame
    cadence and 31.25 Hz bin spacing as the earlier 64 kHz / 2048-sample setup
    while roughly halving the I2S byte rate and FFT length.
- The audio FFT, debug display, and beat LED tasks are pinned to core 1. The
    media SPI task config is pinned to core 0 so the FFT workload is isolated
    from the bus-facing task when SPI is re-enabled.
- The FFT copy buffer is 512 bytes. At 32 kHz / 32-bit mono this is roughly
    4 ms of audio per source read, which keeps individual blocking copy windows
    much shorter than the previous 2048-byte reads.
- Bands are octave-like music bands from 40 Hz to 10 kHz. The first band starts
    at the first bin at or above the configured lower frequency, so idle rumble
    below 40 Hz does not permanently light the sub-bass bar.
- Signal preparation defaults to fixed band calibration and square-root
    compression. Perceptual weighting is optional and disabled by default.
- The magnitude cache applies a small scaled noise gate, then attack/release
    smoothing. The smoothing is intentionally light to keep LED animation
    latency low.

The signal preparation pipeline is deliberately modular so microphone response,
psychoacoustic compensation, and compression can be tested independently. Each
stage is an `std::optional` inside `FftSignalPipelineConfig`; an unset stage is
skipped, and set stages are validated with their own public config type:

- `FftBandCalibrationConfig` applies fixed per-band gains before any adaptive
    scaling.
- `FftPerceptualWeightingConfig` optionally applies A-weighting and blends it
    with the unweighted signal through `amount`.
- `FftMagnitudeCompressionConfig` selects linear, square-root, or `log1p`
    compression. `logScale` tunes only the `log1p` path.

The active 1024-sample / 32 kHz media setting is a CPU/latency tradeoff. It
does not improve sub-bass pitch resolution over the earlier 2048-sample / 64 kHz
setting; if sub-60 Hz discrimination becomes more important than CPU load, use a
longer FFT at the same sample rate.

The media sdkconfig caps `CONFIG_DSP_MAX_FFT_SIZE` at 1024. Audio-tools sizes
the Espressif FFT table from that ESP-DSP maximum rather than the runtime
analyzer length, so keeping the cap aligned with the configured 1024-sample FFT
avoids a larger unused table allocation. This matters once Bluetooth A2DP is
enabled because the classic Bluetooth stack leaves much less contiguous
internal heap.

The magnitude cache is frame-driven instead of task-driven and writes scaled
`0..255` values into each `FftResult`. `FftMagnitudeCacheConfig::mode` selects
the cache strategy:

- `PerBandAdaptive` tracks adaptive floor/peak values independently per band.
    This is useful for visualizers because each band remains expressive even
    when absolute energy differs heavily by frequency.
- `TotalEnergyAdaptive` tracks one adaptive floor/peak over total prepared
    frame energy, then scales every band against that shared range. This keeps
    relative band dominance intact for experiments where per-band adaptation
    hides recent peaks.

The cache is intentionally slow. Its adaptive floor/peak limits are for
environment and volume changes, not for short musical normalization. The floor
is prevented from catching up to the current signal closely enough to erase a
steady tone, so a constant test tone should settle to a stable nonzero band
instead of decaying to black.

The beat tracker runs separate onset detectors for bass, mid, and high groups.
Each `BeatGroupConfig` has its own band range, ambient floor, sensitivity,
onset, refractory, and BPM bounds. Beat detection consumes prepared
`weightedMagnitude`, not display-scaled values, so visual cache tuning does not
move the beat thresholds. A group can trigger on either positive onset flux or a
strong breakout over its slow energy baseline. Candidate beats do not update the
ambient/baseline trackers, so isolated transients do not raise the threshold for
the next real beat. Higher-level musical beat prediction is intentionally not
implemented yet because reliable prediction needs tuning against real audio and
latency measurements.

The `/bpm` console command prints the analyzer's current estimate, last beat
energy, beat count, and last-beat age for every group, and marks the configured
primary group. The GPIO26 indicator LED is driven only by that primary group,
which defaults to bass.

`FftAnalyzerConfig::beatIndicator` is an optional single beat callback slot for
local hardware indicators. `src/media/main.cpp` wires it to a `LedPwm` pulse on
active-high GPIO26 so physical beat timing can be compared against the display
without relying on serial logs.

The analyzer registers metrics through `include/Audio/detail/Metrics.hpp`.
`audCore` keeps rare frame-drop counters, `audFft` keeps diagnostic
copy/readiness/frame/beat counters plus primary-group last beat energy and BPM,
and `audProf` keeps profiling-only timing totals and max subphase durations. The
main metric groups are:

- `copy`, `copyB`, `empty`, `skip`, and `probe` for source feeding.
- `frame` and `beat` in `audFft`, plus `drop` in `audCore`, for analyzer
    output.
- `bandUs`, `cacheUs`, `dispUs`, `btUpdUs`, and `btDisUs` for frame-processing
    phase totals in `audProf`.
- `bandMax`, `cacheMx`, `dispMax`, `btUpdMx`, and `btDisMx` for worst observed
    phase duration in `audProf`.

## SSD1306 Debug Display

`src/media/config.hpp` contains optional I2C SSD1306 display config for realtime
FFT debugging. `enableFftDebugDisplay` is currently disabled in the A2DP media
configuration because Classic Bluetooth, SPI/PubSub, FFT, and the display task
do not leave enough internal heap headroom on the original ESP32. When enabled,
the media node starts `Wire::I2C::Master`, `Wire::I2C::Ssd1306Display`, and
`Audio::FftDisplay` before the analyzer starts.
The analyzer callbacks only update a latest-frame slot; the display task owns
the slow I2C flush so FFT processing does not block on display transfer time.
When `FftDisplayConfig::showRawBands` is enabled, each band is drawn as two
adjacent full-height bars: raw magnitude first, then effective cached/scaled
value. Raw values are normalized to the loudest raw band in the current frame;
effective values use the `0..255` cache output. When raw display is disabled,
only the effective bars are drawn and each band gets the wider display slot.
Beat events draw a short top bar over the related group's band range; the
display intentionally does not draw a full-display beat border.
Display-side metrics live in `dispCore`, `audDisp`, and `dispProf` for frame
drops / flush failures, captured frame and beat counts, and flush timing.
