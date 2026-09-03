# Audio

Audio is split into four components:

- `include/AudioSource/` owns PCM-producing source devices.
- `include/AudioSink/` owns PCM-consuming output devices and transports.
- `include/AudioAfe/` owns the ESP-SR speech front end used by the AI node.
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
- `AudioAfe/Facade.hpp` exports the AFE processor, validated speech-processing
    config, and its synchronous non-owning input/output bindings. The ESP-SR C
    API and model lifecycle remain in `AudioAfe/detail/platform/`.
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
    configurable packet-sized chunks and supports WSS through ESP-IDF's
    transport/TLS layer. Its optional `authorizationHeaderSecretName` points to
    the complete HTTP Authorization header value, including its scheme, so the
    sink does not assume Bearer authentication. WSS expects a PEM root
    certificate pointer in config, for example a Let's Encrypt root supplied
    by the firmware source.

## AI Speech Front End

The AI node has one app-local PCM adapter in `src/ai/pcm16_downsampler.hpp`.
It is intentionally not part of the shared audio component model yet: it adapts
the current SPH0645 source format, 32 kHz 32-bit mono PCM, into the 16 kHz
signed 16-bit little-endian mono PCM required by ESP-SR and the planned NeMo
ASR stream. Because this is an exact 2:1 conversion, the adapter averages
adjacent input samples as a small fixed low-pass/downsample step and clips the
shifted result to PCM16.

The active `env:ai` pipeline is:

```text
SPH0645 -> PCM16 adapter
-> NS(WebRTC)
-> VAD(vadnet1_medium)
-> WakeNet(wn9_alexa, wn9_computer_tts)
-> AGC(WakeNet)
-> assistant turn controller
   | recording: bounded PSRAM ring -> WSS assistant request
   ` response: decoded 24 kHz PCM -> ESP-DSP 24:32 resampler
               -> bounded PSRAM playback ring + zero-crossing onset
               -> continuously clocked I2S playback task -> MAX98357
```

ESP-SR 2.4.6 is pinned through the AI-only `audio_afe_esp_sr` component. The AI
partition table reserves a 6 MiB `model` partition, and the PlatformIO model
scripts pack and flash the selected Alexa, Computer, and VADNet assets during
an ordinary `bin/build -e ai -t upload`. ESP-SR forces its WakeNet AGC mode
whenever WakeNet is active, so the project config validates that combination
explicitly. The selected WebRTC noise suppression stage is intentional for the
cleanup prototype even though ESP-SR logs that NS can reduce recognition
accuracy; it remains a tuning choice rather than an implicit pipeline change.

`AudioAfe::AfeProcessor` continuously feeds and fetches the AFE in complete
chunks. WakeNet is always evaluated and VAD channel-trigger gating is disabled:
VAD never qualifies or suppresses a wake detection. A wake opens the AI-local
`Recording` state and sets the amber status LED immediately. Only VAD speech
observed after that wake, followed by the configured VAD silence transition,
can perform the normal close. The AI latency profile requires 320 ms of
post-speech silence, rather than the former 800 ms portal-oriented setting.
Separate no-speech and maximum-duration timeouts provide bounded fallback
exits.

`AssistantSession` implements one half-duplex utterance per connection. Its
four production states are `WaitingForWake`, `Recording`, `AwaitingResponse`,
and `PlayingResponse`; there is no `Draining` state. The wake frame and every
subsequent cleaned PCM frame through the post-wake VAD endpoint are copied into
a bounded PSRAM byte ring without blocking the AFE task. A core-0 network task
owns all WSS I/O, sends ordered 16 kHz PCM16 mono append events, commits once,
and stops accepting microphone data before response playback can begin.
The session latches a statically configured wake profile before recording:
Alexa selects the `attenborough` voice and Computer selects `bender`. The
selected voice is included in that turn's `session.update`; the same profile
boundary can later own endpoint, timeout, or response-policy selection without
branching on detector indices throughout the session controller.

The assistant client connects to `wss://hal.d-reis.com/v1/realtime`, loads the
complete `Basic ...` header value from the `hal-auth` secret during boot, and
validates the current Let's Encrypt chain against the embedded ISRG Root X1.
Turn-time code reuses that bounded in-memory value so the PSRAM-stack network
task never reads NVS while the flash cache is disabled; changing the secret
requires a restart. TLS is not allowed until the current boot has received an
SNTP synchronization event and the clock passes the configured minimum epoch.
The generic ESP-IDF WebSocket transport handles framing and masking; mbedTLS
handles base64, cJSON validates bounded server events, and bulk buffers, the
network-task stack, and mbedTLS state are allocated in PSRAM.
The connected assistant socket enables `TCP_NODELAY`; the AI-only lwIP profile
uses a 1440-byte MSS, 11,520-byte send/receive windows, larger receive
mailboxes, and WiFi AMPDU in both directions. These settings keep each small
streaming append out of the portal-oriented minimum-memory TCP regime without
changing other node network profiles.
The AI sdkconfig enables mbedTLS PEM parsing for the embedded trust anchor and
keeps ESP-IDF's dynamically allocated WebSocket upgrade buffer at 4 KiB so the
forward-auth response headers fit; the buffer is released after the upgrade.
The assistant uses ESP-IDF's standard WebSocket upgrade and frame path. Because
ESP-IDF includes `:443` in the `Host` authority, the assistant ingress applies
an assistant-only Traefik headers middleware before forward-auth to normalize
both `Host` and the trusted `X-Forwarded-Host` value to `hal.d-reis.com`.
Each upgrade also reports `X-Request-Id: wake-<monotonic-ms>` and the Unix
millisecond time of that wake in `X-Request-Timestamp`. The paired values let
cross-service traces map device monotonic log timestamps onto the server clock
without using UART arrival time as the device timestamp.

Playback begins only after a valid `response.audio.started` event announcing
PocketTTS's 24 kHz mono PCM16 stream. The
[MAX98357 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf)
explicitly lists 24 kHz LRCLK as unsupported, so the node does not drive that
rate directly.
A stateful 4:3 rational FIR from the existing ESP-DSP component converts the
response to supported 32 kHz PCM16. Its 128-tap Kaiser-windowed filter retains
the source bandwidth, has a 10 kHz passband within about 0.002 dB, and rejects
images from 14 kHz by more than 77 dB. The saturated output enters a bounded
512 KiB PSRAM playback ring. A separate core-0 playback task starts after a
configurable 8 KiB/128 ms preroll and owns the optional saturating response gain
and bounded I2S writes. This separation is required: a blocking I2S write must
not pause WebSocket reads and feed TCP backpressure into the audio stream.
The ESP-IDF TX channel is enabled at boot with `auto_clear`, so BCLK/LRCLK
continue and idle DMA descriptors transmit zero PCM. An explicit-prime
path writes configurable 512-byte zero blocks from request commit until the
server announces response audio. It then stops replenishing those blocks so
the short DMA runway drains while the 8 KiB response preroll accumulates. The
former version continued priming through the preroll handoff, but that did not
change the onset transient. The earlier one-second local loopback supplied the
same stronger control and produced the same transient. These observations
exclude I2S/MAX98357 startup and identify the zero-to-arbitrary-PCM boundary as
the common transition. The final PCM path uses audio-tools'
`PoppingSoundRemover` to hold the beginning at digital zero until the first
waveform zero crossing. This avoids ramping an initial offset into the audible
low-frequency `pffff` produced by the rejected linear-fade experiment.
If streaming TTS later starves the queue, playback waits for the same 8 KiB to
rebuild, or for producer completion when less audio remains, before resuming.
Each enqueue wakes the consumer immediately. This recovery policy does not add
first-response latency and avoids replaying isolated tiny chunks after an
upstream synthesis gap.
Purple `Playback` begins only at the queued-audio handoff.
A purple informational `Playback` status is active while server audio plays.
Dim-white `Listening` means the AFE, network, and current-boot clock are all
ready. The LED is explicitly `Off` from the post-wake VAD endpoint until
playback begins; setup never selects generic green `TargetsReady`. Successful
response completion requires audio done, response done, and a normal WebSocket
close before the node returns to `Listening`. A policy-violation close for an
empty submitted sample is an expected outcome, is counted separately, and also
returns directly to `Listening`. Wake detections while a turn is busy are logged
and counted but do not start overlapping capture.

All tuning values live in validated structs assembled in `src/ai/config.hpp`,
including NS/VAD/WakeNet/AGC selection and thresholds, VAD speech/silence and
look-back durations, linear gain, AFE memory/ring/task placement, bounded frame
sizes, fetch wait, session safety timeouts, capture and event capacities,
socket and response timeouts, playback-ring capacity/preroll/prime-write/waits,
zero-crossing onset conditioning, exact response/playback formats, response
gain, SNTP readiness, both assistant-task placements, and
Listening/Recording/Playback statuses.
The `audAfe` metric group reports source/feed/fetch health, detector
transitions, ring headroom, level/peak/RMS/clipping, and failures. `aiAsst`
reports turn/endpoint outcomes, connection failures, captured/sent/played bytes,
capture and playback high water, playback queue depth, queue
underflow/overflow events, maximum producer wait and I2S write duration,
pre/post-conditioning onset magnitude and suppressed sample count, state, and
latency measurements; `aiAsOut.empty` counts expected empty samples. The
per-turn commit logs additionally report exact
endpoint-to-commit time, final-drain PCM and duration, append count,
total/maximum append-send time, and commit-write time; the playback task logs
the first nonzero I2S write relative to the server audio announcement. See the
[AI audio AFE implementation plan](ai-audio-afe-plan.md) and
[assistant WebSocket plan](ai-assistant-websocket-plan.md) for implementation
records and hardware measurements.

The preliminary idle hardware pass after the WebSocket implementation measured
20,407
bytes of free internal data memory with a 19,151-byte low water, 7,257,812
bytes of free PSRAM with a 7,256,080-byte low water, and 0.04% CPU for the idle
assistant task. Its 12,288-byte PSRAM stack retained 10,288 bytes. The AFE used
about 31% of core 1 and retained 98% ring headroom. A later spoken trace verified
ordered capture, one commit, normal WebSocket close, and exact server/device
byte counts. It also exposed the unsupported 24 kHz speaker clock. Converting
to 32 kHz improved the result but did not remove mid-word holes. Timestamp
correlation then showed that synchronous I2S writes paused WebSocket reads even
though PocketTTS generated at 1.9--2.9 times realtime. The bounded playback ring
and dedicated consumer remove that backpressure cycle. The zero-crossing build
uses 124,340 bytes of static RAM and 1,736,591 bytes of application flash; its
512 KiB queue and 512-byte zero block are allocated from PSRAM at boot.
The subsequent latency build uses 124,356 bytes of static RAM and 1,738,915
bytes of application flash. It uploads and reaches Listening cleanly with
19,967 bytes of free internal data memory. Two person-present responses then
completed normally. Device timestamps put endpoint-to-commit at 26 ms and the
first nonzero I2S write 40--52 ms after the server audio announcement. The
active-turn low-water marks were 17,223 internal bytes and 6,648,080 external
bytes. A 6.08-second response delivered over 7.122 seconds exposed five
upstream-starvation intervals with a 1.116-second maximum and a 199,808-byte
eventual queue high-water; that observation led to the adaptive rebuffer policy
above. Its recovery behavior still needs a similarly bursty response to recur.
The dual-WakeNet voice-profile build uses 124,428 bytes of static RAM and
1,745,039 bytes of application flash. On clean boot it retains a 12,867-byte
internal-data low water and 6,368,876 external bytes while the AFE consumes
about 40--42% of core 1 with 98% ring headroom and no failures. Subjective onset
quality and spoken verification of both profile selections remain
person-present checks.

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

The active `env:media` board is an ESP32-S3 Zero. Its SPH0645 link uses GPIO13
for BCLK, GPIO1 for WS/LRCK, and GPIO12 for data input. The S3 uses native USB
Serial/JTAG for its console so the schematic's GPIO44 indicator is not claimed
by a hardware UART.

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
`ExpectedHit`, `ExpectedMiss`, `Reacquired`, and `Lost`. The initial tracker is
a small deterministic inter-onset/phase tracker, not the final EDM-grade tracker;
it is intentionally instrumented so WAV fixtures and live input can show whether
the evidence group, BPM bounds, hit window, and confidence rules are plausible.

The `/peaks` console command prints the analyzer's current peak rate estimate,
last peak energy, peak count, and last-peak age for every group, and marks the
configured indicator group. The `/tempo` command prints tracker lock state, BPM,
confidence, last beat outcome, and hit/miss/reacquire/lost counters. The GPIO44
indicator LED is driven only by the peak indicator group, which defaults to bass.
Tempo confidence is capped by recent expected-hit / expected-miss stability, so
a consistent but frequently lost/reacquired lock cannot report full confidence.
While locked, off-window peak events do not update the tempo interval history.
Candidate tempo intervals must also agree with the evidence group's own peak
rate estimate, allowing octave-related half/double-time matches, so isolated
short bass transients do not override a slower stable peak cadence.

`FftAnalyzerConfig::peakIndicator` is an optional single peak callback slot for
local hardware indicators. `src/media/main.cpp` wires it to an electrically
active-low `LedPwm` pulse on GPIO44 so physical peak timing can be compared
against the display without relying on serial logs.

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
build. The v2 bus uses GPIO5 SCL and GPIO6 SDA, external pull-ups, and the
owner-selected 1 MHz display overclock; MCU internal pull-ups remain disabled.
When enabled, the media node starts
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
