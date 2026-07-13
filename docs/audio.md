# Audio

Audio is split into three components:

- `include/AudioSource/` owns PCM-producing source devices.
- `include/AudioSink/` owns PCM-consuming output devices and transports.
- `include/AudioFft/` owns FFT analysis, peak/tempo extraction, wire payloads,
    metrics, and the optional debug display.

## Component Shape

- `AudioSource/Facade.hpp` exports the common source surface without requiring
    FFT headers. Source configs live in `AudioSource/Interfaces/`.
- `AudioSource/detail/Sources/` owns the concrete audio inputs. The media app
    instantiates exactly one source object and passes it to
    `AudioFft::FftAnalyzer` through `AudioSource::IAudioSource`.
- `AudioSink/Facade.hpp` exports the common sink surface. Sink configs live in
    `AudioSink/Interfaces/`, while concrete sinks live in
    `AudioSink/detail/Sinks/`.
- `AudioFft/Interfaces/Types.hpp` contains FFT frame, magnitude scaling,
    peak/accent, and tempo-clock beat value types.
- `AudioFft/detail/FftAnalyzer.hpp` feeds the source into arduino-audio-tools'
    stream/sink FFT path, calculates fixed FFT bands, updates the magnitude
    cache, and emits frame/peak/beat callbacks.
- `AudioFft/detail/FftBackend.hpp` selects the active arduino-audio-tools FFT
    implementation and exposes it to `FftAnalyzer` through the common
    `AudioFFTBase` surface.
- `AudioFft/detail/FftDisplay.hpp` is an optional media-node debug visualizer for
    a 128x32 SSD1306 I2C display. It subscribes to FFT frames and peaks but
    flushes the display from its own task.
- `AudioSource/detail/platform/PlatformESP32.hpp` owns source-side
    arduino-audio-tools stream and I2S glue.
- `AudioSink/detail/platform/PlatformESP32.hpp` owns sink-side
    arduino-audio-tools I2S glue plus TCP/WebSocket output streams.
- `AudioFft/detail/platform/PlatformESP32.hpp` owns FFT backend, window, and
    stream-copy aliases.
    Bluetooth source headers include their stack-specific dependencies only
    when that source is selected.

The current design supports one simultaneous audio source. `src/media/config.hpp`
keeps source-specific configs near each other for switching, but the selected
source is a compile-time type choice through `mediaAudioSourceKind` and
`MediaAudioSourceBinding`. There is no aggregate source object that owns I2S,
WAV, Bluedroid, and BTstack members at the same time.

## Audio Sources

`mediaAudioSourceKind` selects the active source:

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
- `AudioSourceKind::BtstackA2DP` uses `BtstackA2DPSource` as an experimental
    Bluetooth A2DP sink through BlueKitchen BTstack. BTstack and Bluedroid
    cannot run as runtime-selectable alternatives in one firmware because
    BTstack requires the ESP-IDF Bluetooth controller-only configuration while
    `pschatzmann/ESP32-A2DP` uses Bluedroid. The source starts a dedicated
    BTstack FreeRTOS task, decodes SBC through BTstack's Bluedroid-derived SBC
    decoder, and writes mono or stereo 16-bit PCM into the same fixed ring
    stream used by `A2DPSource`.

Only the selected source type is instantiated. Inactive source configs in
`src/media/config.hpp` are `constexpr` values for easy switching and do not
create BSS objects. The active `env:media` build currently selects I2S and keeps
Bluetooth disabled so Bluetooth SDK/library globals do not consume media-node
DRAM. Selecting a Bluetooth source also requires including the matching concrete
source header in `src/media/config.hpp` and enabling the corresponding
SDK/library dependencies for that firmware.

`bin/wavgen.py` writes short LittleFS-friendly fixtures to
`data/media/littlefs/test.wav` by default. Upload the filesystem image before
testing `WavFile` so the mounted LittleFS contains the sample.

## Audio Sinks

The initial sinks mirror the source model: they expose an audio-tools
`AudioStream` and consume PCM bytes described by `AudioSink::AudioInfo`.

- `I2SSink` writes PCM to an ESP32 I2S TX peripheral. Its ready-made
    `MAX98357` preset uses ESP32-provided BCLK/WS, Philips I2S, 44.1 kHz,
    16-bit stereo PCM.
- `TcpSink` is an outbound TCP client. It connects lazily to a configured IPv4
    endpoint and writes the PCM byte stream directly for local diagnostics or
    home-network use.
- `WebSocketSink` is an outbound WebSocket client. It sends binary frames in
    configurable packet-sized chunks, supports `Authorization: Bearer ...`, and
    supports WSS through ESP-IDF's transport/TLS layer. WSS expects a PEM root
    certificate pointer in config, for example a Let's Encrypt root supplied by
    the firmware source.

Enabling `A2DPSource` requires the ESP-IDF Bluedroid Bluetooth stack. On the
original 4 MiB ESP32 media board this has a large flash-size cost, so Bluedroid
A2DP should be treated as a diagnostic input until the linked firmware size is
validated. The BTstack alternative pins BTstack to a known commit, ignores
BTstack as a normal PlatformIO library, and builds only the classic A2DP sink
sources through `components/btstack`. `components/btstack_config` keeps
BTstack's pools static and small for one A2DP connection.

BTstack is viable here as a compile-time source/backend selection, not as an
in-process plugin next to Bluedroid. It also carries BlueKitchen's
non-commercial/personal-use license clause in the source files used for A2DP
and SBC decoding, so any commercial use would need a license review before this
becomes a product direction.

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
the separate `weightedMagnitude` value used by the magnitude cache and peak
detector. This avoids the legacy double-averaging bug where band sums were
averaged once in the callback and then divided by bin count again during
scaling.

`FftAnalyzerConfig::backend` selects the FFT library. `EspressifFft` is the
default because it uses Espressif's DSP FFT implementation through
`AudioEspressifFFT.h`; `RealFft` remains available for comparison against
arduino-audio-tools' portable real FFT path. Both backends feed the same
`AudioFFTBase` callback into the analyzer, so band reduction, scaling, peak
extraction, display output, and metrics stay backend-independent. The ESP-DSP
component dependency is declared in `src/idf_component.yml`.

The analyzer defaults are conservative public API defaults. The active media
node config in `src/media/config.hpp` is tuned for low-latency visualization
without monopolizing the SPI-facing core:

- The SPH0645 preset runs at 32 kHz. The analyzer uses
    1024-sample FFT frames and stride, which keeps the same roughly 32 ms frame
    cadence and 31.25 Hz bin spacing as the earlier 64 kHz / 2048-sample setup
    while roughly halving the I2S byte rate and FFT length.
- The audio FFT, debug display, and peak indicator LED tasks are pinned to core
    1. The media SPI task config is pinned to core 0 so the FFT workload is
    isolated from the bus-facing task when SPI is re-enabled.
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

The peak detector runs separate onset detectors for bass, mid, and high groups.
Each `PeakGroupConfig` has its own band range, ambient floor, sensitivity, onset,
refractory, and rate bounds. Peak detection consumes prepared
`weightedMagnitude`, not display-scaled values, so visual cache tuning does not
move the peak thresholds. A group can trigger on either positive onset flux or a
strong breakout over its slow energy baseline. Candidate peaks do not update the
ambient/baseline trackers, so isolated transients do not raise the threshold for
the next transient.

Peak events are not tempo beats. They are local accent/transient observations for
animation accents, tracker evidence, and diagnostics. The first tempo-tracking
stage consumes the configured evidence group, currently bass by default, and
maintains one explicit beat clock. That clock publishes `BeatEvent` outcomes:
`ExpectedHit`, `ExpectedMiss`, `Reacquired`, and `Lost`. The initial tracker is a
small deterministic inter-onset/phase tracker, not the final EDM-grade tracker;
it is intentionally instrumented so WAV fixtures and live input can show whether
the evidence group, BPM bounds, hit window, and confidence rules are plausible.

The `/peaks` console command prints the analyzer's current peak rate estimate,
last peak energy, peak count, and last-peak age for every group, and marks the
configured indicator group. The `/tempo` command prints tracker lock state, BPM,
confidence, last beat outcome, and hit/miss/reacquire/lost counters. The GPIO26
indicator LED is driven only by the peak indicator group, which defaults to bass.
Tempo confidence is capped by recent expected-hit / expected-miss stability, so
a consistent but frequently lost/reacquired lock cannot report full confidence.
While locked, off-window peak events do not update the tempo interval history.
Candidate tempo intervals must also agree with the evidence group's own peak
rate estimate, allowing octave-related half/double-time matches, so isolated
short bass transients do not override a slower stable peak cadence.

`FftAnalyzerConfig::peakIndicator` is an optional single peak callback slot for
local hardware indicators. `src/media/main.cpp` wires it to a `LedPwm` pulse on
active-high GPIO26 so physical peak timing can be compared against the display
without relying on serial logs.

The analyzer registers metrics through `include/AudioFft/detail/Metrics.hpp`.
`audCore` keeps rare frame-drop counters, `audFft` keeps diagnostic
copy/readiness/frame/peak/beat counters plus indicator-group last peak energy,
peak rate, BPM, and tempo confidence, and `audProf` keeps profiling-only timing
totals and max subphase durations. The profiling group is disabled by default to
leave Bluetooth builds with more internal-heap headroom; define
`TOTEM_ENABLE_AUDIO_PROFILING_METRICS=1` for a
temporary profiling firmware when investigating FFT phase timings. The
main metric groups are:

- `copy`, `copyB`, `empty`, `skip`, and `probe` for source feeding.
- `frame`, `peak`, `pkBass`, `pkMid`, `pkHigh`, `beat`, `btHit`, `btMiss`,
    `btReq`, and `btLost` in `audFft`, plus `drop` in `audCore`, for analyzer
    output.
- `peakE` and `pkRate` in `audFft` for the latest indicator-group peak energy
    and estimated peak rate per minute.
- `bpm` and `btConf` in `audFft` for the current tempo estimate and confidence.
- `calReq`, `calFrm`, `calDone`, and `calAct` in `audFft` for background-floor
    calibration requests, sampled frames, completions, and active state.
- `bandUs`, `cacheUs`, `dispUs`, `pkUpdUs`, `pkDisUs`, `tmpUpdUs`, and `btDisUs`
    for frame-processing phase totals in `audProf`.
- `bandMax`, `cacheMx`, `dispMax`, `pkUpdMx`, `pkDisMx`, `tmpUpdMx`, and
    `btDisMx` for worst observed phase duration in `audProf`.

## SSD1306 Debug Display

`src/media/config.hpp` contains optional I2C SSD1306 display config for realtime
FFT debugging. `enableFftDebugDisplay` currently enables it for the I2S media
build. Bluetooth media builds may need to disable it because Classic Bluetooth,
SPI/PubSub, FFT, and the display task can leave too little internal heap
headroom on the original ESP32. When enabled, the media node starts
`Wire::I2C::Master`, `Wire::I2C::Ssd1306Display`, and `AudioFft::FftDisplay`
before the analyzer starts.
The analyzer callbacks only update a latest-frame slot; the display task owns
the slow I2C flush so FFT processing does not block on display transfer time.
When `FftDisplayConfig::showRawBands` is enabled, each band is drawn as two
adjacent full-height bars: raw magnitude first, then effective cached/scaled
value. Raw values are normalized to the loudest raw band in the current frame;
effective values use the `0..255` cache output. When raw display is disabled,
only the effective bars are drawn and each band gets the wider display slot.
Peak events draw a short top bar over the related group's band range; the
display draws beat outcomes as a separate full-width bottom bar under the FFT
bands. `Reacquired` uses the configured full beat-bar height, `ExpectedHit` uses
a shorter solid bar, and `ExpectedMiss` / `Lost` use a one-pixel bar so tempo
state is visible without confusing beat outcomes with per-group peak markers.
Display-side metrics live in `dispCore`, `audDisp`, and `dispProf` for frame
drops / flush failures, captured frame, peak, and beat counts, and flush timing.

## Host Audio Viewer

`bin/pubsub-audio-view` is a local Pygame consumer for the UDP bridge's generic
Unix-domain socket fanout. Start the bridge with `--local-socket`, then start
the viewer against the same path:

`bin/pubsub-udp-peer --mcu-ip <master-ip> --bind-ip <host-ip> --local-socket /tmp/totem-pubsub.sock`

`bin/pubsub-audio-view --socket /tmp/totem-pubsub.sock`

The viewer subscribes to the numeric `Beat`, `FftFrame`, and `Peak` topic masks
through the local socket. Audio wire decoding is contained in the viewer
consumer; the generic UDP bridge still forwards arbitrary PubSub events as
`header` plus `payload_hex` and does not know about FFT, peak, or beat payloads.
Pressing `c` in the viewer publishes the existing `Button` topic payload for a
pressed `PeripheralButton::Calibration`, matching the IO GPIO path.
