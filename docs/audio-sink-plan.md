# Audio Sink Split Plan

## Goal

Add an `AudioSink` component analogous to `AudioSource`, so PCM-producing
sources can be connected to output devices or transports without depending on
the FFT setup.

The first sink implementations are:

- I2S output, with a ready-made MAX98357 preset.
- TCP output, for local debugging and home use.
- WebSocket output, sharing the TCP networking shape where possible, with
  configurable packet size, optional bearer-token authentication, and WSS.

This task should not add DSP features, stream metadata, buffering policies, or
source/sink orchestration. Sinks should accept PCM bytes in the same audio-tools
stream style already used by sources and FFT.

## Current Findings

- `AudioSource` already exposes the current PCM description through
  `AudioSource::AudioInfo`.
- Source implementations expose an `audio_tools::AudioStream` through
  `IAudioSource::stream()`. FFT consumes that stream directly.
- I2S source support is implemented through a small ESP32 platform wrapper in
  `AudioSource/detail/platform/PlatformESP32.hpp`.
- Existing networking provides an outbound TCP client abstraction:
  `Network::detail::DefaultTcpClient` and `DefaultTcpConnection`.
- The current `Network::Ipv4Endpoint` is IP-address based. That is enough for
  local TCP and insecure WebSocket connections, but not enough for clean WSS
  host headers, SNI, or certificate validation.
- ESP-IDF in this checkout does not appear to provide `esp_websocket_client`.
  It does provide the lower-level transport APIs, including WebSocket transport
  headers behind `CONFIG_WS_TRANSPORT`.
- TLS and WebSocket transport are currently disabled in the shared ESP32 stack
  defaults and in the generated `ai` sdkconfig. The user approved changing the
  `ai` SDK configuration for this feature.

## Assumptions

Blocking assumptions resolved by the user:

- TCP and WebSocket sinks are outbound clients.
- WSS should use certificate validation as the expected path.
- Supplying a PEM trust root from a header is sufficient. The immediate
  expected use is a Let's Encrypt root.
- Adding hostname support is acceptable if it stays small.
- SDK configuration changes for `ai` are acceptable.

Non-blocking implementation assumptions:

- The MAX98357 preset will use ESP32 as I2S clock master, Philips I2S,
  44.1 kHz, 16-bit stereo PCM.
- WebSocket writes will send binary frames.
- `write(data, len)` on network sinks may return `0` if not connected or if a
  connection attempt fails. The sink will close failed connections and retry on
  a throttled reconnect interval.
- WebSocket packet size means maximum payload bytes per WebSocket binary frame.

## Planned Component Shape

Create a new `include/AudioSink/` tree:

- `AudioSink/Facade.hpp`
- `AudioSink/Interfaces/Types.hpp`
- `AudioSink/Interfaces/SinkConfig.hpp`
- `AudioSink/detail/PlatformSelect.hpp`
- `AudioSink/detail/Sinks/IAudioSink.hpp`
- `AudioSink/detail/Sinks/I2SSink.hpp`
- `AudioSink/detail/Sinks/TcpSink.hpp`
- `AudioSink/detail/Sinks/WebSocketSink.hpp`
- `AudioSink/detail/platform/PlatformESP32.hpp`

The public sink interface should stay minimal:

```cpp
struct IAudioSink {
    virtual ~IAudioSink() = default;
    [[nodiscard]] virtual bool active() const = 0;
    [[nodiscard]] virtual const AudioInfo &audioInfo() const = 0;
    [[nodiscard]] virtual bool ready() const = 0;
    [[nodiscard]] virtual const char *sinkName() const = 0;
    virtual Platform::AudioStream &stream() = 0;
};
```

`AudioSink::AudioInfo` will initially alias the existing
`AudioSource::AudioInfo` to avoid a broader PCM-type refactor in this task. A
future cleanup can promote shared PCM/I2S types out of `AudioSource` if the
source/sink split proves stable.

## I2S Sink Plan

Add:

- `I2SOutputPins` with bit clock, word select, and data-out pins.
- `I2SSinkDevicePreset` with `MAX98357` and `Custom`.
- `I2SSinkConfig` with `resolvedLink()` similar to `I2SSourceConfig`.
- `max98357I2SLinkConfig()` returning the ready-made MAX98357 link config.

The ESP32 platform wrapper will add an `I2SOutputStream` next to the existing
input wrapper. It will configure audio-tools in `TX_MODE`, map the output data
pin, expose `stream()`, and keep lifecycle behavior consistent with sources.

## TCP Sink Plan

Add a small shared network sink config:

- IP endpoint.
- Optional hostname string for transports that need one.
- Connect timeout.
- Reconnect interval.
- Audio format.

The TCP sink stream will:

- Connect lazily on first write.
- Reuse `Network::detail::DefaultTcpClient` and `DefaultTcpConnection`.
- Write PCM bytes directly with `sendAll`.
- Close and mark not-ready after send/connect failure.
- Avoid background tasks and extra queues.

## WebSocket Sink Plan

The WebSocket sink will reuse the shared network sink config and add:

- Path.
- Secure/insecure mode.
- Optional bearer token.
- PEM root certificate string for WSS.
- Packet size.

Implementation target:

- Use ESP-IDF lower-level transport APIs (`tcp_transport`, WebSocket transport,
  and TLS transport for WSS) rather than adding a new external dependency.
- Send binary frames in chunks no larger than the configured packet size.
- Set `Authorization: Bearer <token>` when configured.
- Use hostname for `Host` and SNI when provided; otherwise fall back to the IP
  string.
- Require `trustedRootPem` when WSS is enabled. Insecure WSS is not part of
  this implementation.

Build wiring should be scoped to `ai` as much as possible:

- Enable `CONFIG_WS_TRANSPORT` and TLS client support in `sdkconfig.stack.ai`.
- Add required ESP-IDF components for `ai` only in `src/CMakeLists.txt`, if the
  transport headers require explicit `REQUIRES`.
- Do not change the shared ESP32 stack defaults for media/master/io/gpu.

## Documentation Updates

Update `docs/audio.md` or `docs/structure.md` after implementation to mention:

- `AudioSource` owns input devices.
- `AudioSink` owns output devices/transports.
- `AudioFft` consumes sources but is no longer the conceptual owner of I2S
  audio devices.

## Validation Plan

Run:

- `bin/build -e ai`
- `bin/build -e media`

The `ai` build should validate the SDK/TLS/WebSocket path. The `media` build
should confirm the existing source/FFT split still compiles without dragging in
the new sink transport costs.

If the WebSocket implementation is header-only and not naturally included by an
active source file, add only the smallest compile-time include/use needed in
`ai` to validate it. Avoid adding persistent diagnostic environments.

## Non-Goals

- No audio routing framework.
- No metadata envelope around PCM.
- No source-to-sink task orchestration.
- No buffering beyond the WebSocket packet-size chunking required by the sink.
- No changes to Bluetooth source behavior.
- No changes to FFT behavior except documentation references if needed.
