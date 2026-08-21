# AI assistant WebSocket plan

## Goal

Replace the delayed microphone loopback with a half-duplex assistant turn:

1. ESP-SR continues to run WakeNet and VAD continuously.
2. WakeNet opens a recording turn without consulting VAD.
3. Every cleaned PCM frame from that point through the post-wake VAD endpoint
   is sent to the assistant service.
4. The client commits the input, stops uploading microphone audio, and plays
   the streamed response through the MAX98357.
5. After the server finishes its response, the node returns to wake-word
   detection for the next independent turn.

Remove the one-second loopback, its delay storage, configuration, metrics, and
diagnostic routing completely. The production turn model has no `Draining`
state or status.

Implementation status: implemented and validated through a complete spoken
turn. Build, flash, clean boot, SNTP, WakeNet/VAD capture, authenticated WSS,
one commit, response playback, normal close, LED transitions, and exact
server/device byte counts are exercised. Converting PocketTTS's unsupported
24 kHz speaker clock to 32 kHz improved playback but left mid-word holes.
Timestamp correlation isolated those to synchronous I2S writes pausing
WebSocket reads and applying TCP backpressure. A dedicated playback task and
bounded PSRAM queue are now flashed; subjective replay is the remaining
person-present check.

## Findings

### Target service contract

The target API is the assistant source currently on disk under
`../../ai/assistant`. The wire contract below is the only service detail the
firmware needs. It does not inspect a service revision, negotiate protocol
variants, or retain compatibility paths for earlier API shapes.

- Public endpoint: `wss://hal.d-reis.com/v1/realtime`.
- FastAPI accepts one utterance per WebSocket and closes it with code 1000 after
  the response.
- Traefik terminates TLS with a cert-manager Let's Encrypt certificate and
  forwards the normal WebSocket upgrade.
- The ingress is protected by the shared Authentik forward-auth middleware.
- The service accepts text WebSocket messages up to 2 MiB.
- Upstream connect timeout is 10 seconds and the general request timeout is
  120 seconds. The ingress adds no assistant-specific timeout override.

The client must first send:

~~~json
{"type":"session.update","session":{"input_audio_sample_rate":16000,"input_audio_channels":1,"voice":"attenborough"}}
~~~

The voice is selected by the latched wake profile. Alexa sends
`attenborough`; Computer sends `bender`.

Microphone PCM is signed 16-bit little-endian mono at 16 kHz. Each chunk is
base64-encoded in a text event:

~~~json
{"type":"input_audio_buffer.append","audio":"<base64 pcm16le>"}
~~~

End of input is explicit:

~~~json
{"type":"input_audio_buffer.commit"}
~~~

Commit is final for that connection. The service stops reading client events,
finishes transcription, generates the response, and then closes the socket.

The server forwards transcription events and can interleave response text and
audio events. The embedded client only needs to recognize errors and these
audio lifecycle events:

~~~json
{"type":"response.audio.started","format":"pcm16","sample_rate":24000,"sample_width":2,"channels":1}
{"type":"response.audio.delta","audio":"<base64 pcm16le>"}
{"type":"response.audio.done"}
{"type":"response.done"}
~~~

The sample rate in `response.audio.started` is authoritative. The current
PocketTTS path reports 24 kHz, 16-bit mono. The
[MAX98357 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf)
does not support 24 kHz LRCLK, so the firmware validates that exact source
format and converts it to supported 32 kHz before I2S playback rather than
driving the announced rate directly.

`response.audio.done` means the server will send no more speech.
`response.done` and the following normal WebSocket close confirm completion
of the entire turn.

### Desktop reference

`~/.local/gitbin/ai assist` exercises this same contract successfully. It
captures 16 kHz PCM16 mono in 8,000-byte chunks, sends `session.update`, sends
base64 append events while recording, commits when recording stops, and
receives events concurrently. It starts playback only after
`response.audio.started`, validates the announced format, decodes each audio
delta, handles fragmented text and ping/pong frames, and finishes when the
server closes. The embedded client replaces its Ctrl-C endpoint with the
existing post-wake VAD endpoint.

### Connection lifetime

Keep one WebSocket per utterance. Measured TLS/WebSocket setup was roughly
0.8 seconds and completed while the user was still speaking, so it did not add
to commit-to-first-audio latency. Holding an idle connection open would not
materially improve the observed turn while it would require a different server
session contract, idle liveness handling, and reconnect state. Revisit this
only if later short commands routinely finish before the upgrade completes.

### Authentication

The desktop script obtains an interactive Authentik token and sends
`Authorization: Bearer <token>`.

The Kubernetes catalog separately provisions an Authentik service account named
`esp32` for this application. Its Vault record is
`k8s/assistant/machine-auth/esp32`, and its ready-made
`authorization` field has this form:

~~~text
Basic <base64 machine credential>
~~~

The firmware must store and send that complete authorization header value. In
this document, "header value" means `Basic ...`, without the literal
`Authorization:` field name. Both the generic WebSocket sink and the assistant
client pass it to ESP-IDF's WebSocket transport unchanged; ESP-IDF adds the
field name. The firmware must not prepend `Bearer` or otherwise interpret the
value.

The Basic payload must pair the actual Authentik service-account username with
its app password. The reserved username `goauthentik.io/token` selects
Authentik's bearer-token path and therefore must not be paired with an
`app_password` token, as documented by
[Authentik header authentication](https://docs.goauthentik.io/add-secure-apps/providers/proxy/header_authentication).
A suitable NVS key is `hal-auth`, which fits the NVS 15-character key limit.
Provisioning can use the existing command with a placeholder value:

~~~text
/secret set hal-auth Basic <base64-machine-credential>
~~~

The command joins value tokens with spaces, so the stored secret is the exact
header value. The credential, base64 audio, and transcripts must never be
written to normal logs.

### Pre-implementation firmware

The active AI path already produces the correct input format. Its final two
debug stages are the part removed by this implementation:

~~~text
SPH0645 -> 16 kHz PCM16 adapter
-> NS(WebRTC) -> VAD -> WakeNet(Alexa) -> AGC
-> wake/VAD session -> one-second debug delay -> MAX98357
                       ^ remove this local loopback path completely
~~~

The established session rules remain correct:

- WakeNet is evaluated continuously and is never gated by VAD.
- WakeNet opens `Recording` immediately and sets the amber status.
- The frame that reports the wake is included in the recording.
- After wake-up, VAD is used only to find the command endpoint.
- Once open, the stream remains continuous through short silence; individual
  VAD-silence frames are not removed.
- Normal completion requires post-wake speech followed by the configured VAD
  silence transition.
- No-speech and maximum-duration timeouts remain bounded safety exits.

The existing `AudioSink::WebSocketSink` is not the assistant protocol:

- it is write-only;
- it sends raw binary WebSocket frames;
- it has no receive/event path;
- it reconnects lazily, which could split one utterance across connections;
- its authentication surface is named specifically for Bearer tokens.

Changing that generic PCM sink into a service-specific duplex client would
blur its contract and risk other users. The assistant should therefore use a
small AI-local WebSocket turn client built on the same ESP-IDF transport APIs.

The generic sink's authentication API should still be corrected as part of
this task. Use names that say the secret contains the complete header value:

- rename `bearerTokenSecretName` to `authorizationHeaderSecretName`;
- rename `hasBearerToken()` to `hasAuthorizationHeader()`;
- rename `webSocketMaxBearerTokenBytes` to
  `webSocketMaxAuthorizationHeaderBytes`;
- rename `_prepareAuth()` and `_authHeader` to
  `_prepareAuthorizationHeader()` and `_authorizationHeader`;
- read the complete header value from SecretStorage, copy it without a prefix
  into the bounded NUL-terminated buffer, and pass it to
  `esp_transport_ws_set_auth()` unchanged.

Require a non-empty value and reject embedded NUL, carriage-return, or newline
bytes so a provisioned secret cannot inject another HTTP header. Otherwise do
not trim, split, decode, re-encode, or inspect the authorization scheme.

Do not keep deprecated fields, aliases, prefix detection, or a fallback that
constructs `Bearer`. This repository has no compatibility requirement for
the old names or semantics.

### Resource baseline

Before adding an active TLS connection, the validated AI build reported:

- 116,604 bytes static RAM and 1,509,287 bytes flash;
- approximately 19,079 bytes minimum free internal memory;
- approximately 7.6 MiB minimum free PSRAM;
- 3,200 / 8,192 bytes used by the AFE task stack;
- approximately 5.1% / 35.0% core utilization.

The internal-memory margin is too small for default internal-only mbedTLS
buffers plus another task stack. Bulk capture, event, and TLS allocations must
therefore be deliberately placed in PSRAM and measured on hardware.

## Target behavior

The active assistant path becomes:

~~~text
microphone -> PCM adapter -> ESP-SR AFE
                         |
                         +-> WakeNet/VAD/session control
                         |
                         +-> bounded raw-PCM capture ring
                              -> WSS JSON/base64 input
                              -> commit at the VAD endpoint

WSS JSON/base64 response
-> validate announced PCM format
-> bounded block decode
-> ESP-DSP 24 kHz to 32 kHz rational FIR
-> MAX98357
~~~

The microphone and AFE continue running while the response plays, but no
microphone PCM is uploaded during response generation or playback. Wake
detections can still be observed and counted while busy, but they do not open a
second turn. This is a turn-state restriction, not VAD gating.

## Turn state machine

The production state machine has four states:

| State | Microphone upload | Speaker | Exit |
| --- | --- | --- | --- |
| `WaitingForWake` | Off | Off | WakeNet detection |
| `Recording` | Cleaned session PCM | Off | VAD endpoint or safety/error exit |
| `AwaitingResponse` | Off | Off | Audio start, no-audio completion, or error |
| `PlayingResponse` | Off | Remote PCM | Audio/response completion or error |

Transitions:

1. `WaitingForWake -> Recording`
   - Accept WakeNet regardless of current or previous VAD state.
   - Reset the capture ring and per-turn protocol state.
   - Keep the dim-white `Listening` informational state underneath the
     higher-priority recording indication.
   - Set the existing amber `Recording` status.
   - Signal the network task to establish a new WSS connection.
   - Queue the wake result's cleaned PCM frame immediately.

2. While `Recording`
   - Queue every cleaned frame, not only frames marked VAD speech.
   - Track post-wake speech and endpoint state exactly as today.
   - Repeated wakes are logged/counted but do not restart the turn.
   - The network task drains the ring into ordered append events.

3. Normal VAD endpoint
   - Stop accepting PCM for this turn.
   - Reset `Recording` immediately.
   - Select informational `Off` so amber clears without falsely indicating
     wake-word readiness while the response is pending.
   - Enter `AwaitingResponse`.
   - Let the network task send any already-queued tail, then exactly one
     `input_audio_buffer.commit`.

4. No-speech timeout
   - Reset `Recording`, discard the buffered turn, and close without commit.
   - Do not ask the server to transcribe a wake with no post-wake command.

5. Maximum-duration timeout
   - Commit if post-wake speech was observed and the capture is intact.
   - Otherwise abort as no-speech.

6. `AwaitingResponse -> PlayingResponse`
   - Require one valid `response.audio.started`.
   - Validate 24 kHz mono PCM16, resample it to supported 32 kHz mono PCM16,
     enqueue it without blocking WebSocket reception, and keep I2S at that
     hardware playback format.
   - Start the dedicated playback task after the configured preroll and replace
     `Off` with the configured purple informational `Playback` LED state.

7. Response completion
   - `response.audio.done` closes the remote audio stream.
   - Finish the final successful I2S write, consume `response.done`, and
     accept the server's normal close.
   - Reset `Playback`, restore `Listening`, and return directly to
     `WaitingForWake`.
   - Do not introduce a separate `Draining` state or LED status.

8. Any fatal network, TLS, authentication, protocol, buffer, or playback error
   - Close the socket once, clear the turn buffers and LED states, record the
     reason, and return to `WaitingForWake`.
   - Never reconnect after any audio from the turn has been sent. A retry would
     create a different, truncated utterance.

## Component shape

Keep the assistant protocol and turn behavior local to `src/ai/`:

- `assistant_websocket.hpp`
  - one WSS connection;
  - complete Authorization header-value secret loading;
  - TLS root configuration;
  - text-frame send and fragmented text-message receive;
  - narrow bounded event encoding/decoding;
  - close/error classification.
- `assistant_session.hpp`
  - the four-state turn controller;
  - the AFE frame binding;
  - the PSRAM capture ring;
  - separate network and playback task ownership and notifications;
  - the bounded PSRAM playback ring and starvation/overflow telemetry;
  - response playback and turn metrics.
- `pcm16_playback_resampler.hpp`
  - fixed 24-to-32 kHz rational conversion through ESP-DSP;
  - state retained across WebSocket deltas;
  - bounded conversion blocks and saturating PCM16 output.
- `audio_session.hpp`
  - deleted after moving the WakeNet/VAD endpoint logic into
    `assistant_session.hpp`.
- `config.hpp`
  - assemble all endpoint, timeout, buffering, task, status, and playback
    values as validated constexpr configuration.
- `main.cpp`
  - instantiate only the assistant session;
  - rename loopback-oriented speaker objects/configuration for their actual
    response-playback role.

Remove `DelayedPlaybackConfig`, `DelayedPlayback`,
`delayedPlaybackConfig`, `max98357LoopbackSinkConfig`, the PSRAM delay line,
waiting-state zero fill, and the loopback-only `fwdB`, `zeroB`, `playSamp`, and
`sinkDrop` metrics. Replace the sink configuration with a response-playback
name and purpose; do not retain a compile-time loopback option or a dormant
diagnostic route.

The shared `AudioSink` files receive the breaking authorization rename in
`Interfaces/Types.hpp`, `Interfaces/SinkConfig.hpp`, and
`detail/platform/PlatformESP32.hpp`. `docs/audio.md` is updated to describe a
caller-supplied Authorization header value instead of a Bearer token. No other
generic WebSocket sink behavior changes.

Do not add a general routing framework or change `AudioAfe` frame ownership.
Its synchronous, non-owning frame callback remains appropriate: the callback
copies active-turn samples into the bounded capture ring and returns without
performing DNS, TLS, JSON, or socket work.

### Network task

Use one dedicated task, preferably on core 0 so TLS/JSON work does not compete
with the core-1 AFE. It owns the WebSocket handle and all socket operations.

On turn start it should:

1. confirm WiFi has a station address and TLS time is valid;
2. use the complete authorization header value loaded from SecretStorage
   during boot;
3. connect to `hal.d-reis.com:443` with SNI and root validation;
4. let ESP-IDF perform the standard WebSocket upgrade and require HTTP 101;
5. send `session.update`;
6. aggregate queued PCM into bounded append messages;
7. send the partial final chunk, then commit;
8. receive events until response completion and normal close.

Only one task may read or write the WebSocket. Control events from the AFE path
should use task notifications or atomic flags; PCM uses an ESP-IDF ring buffer
allocated explicitly from PSRAM.

### Event handling

Prefer the optimized, maintained SDK implementations over local versions:

- ESP-IDF TLS transport plus `esp_transport_ws` for the HTTP upgrade, framing,
  masking, control frames, and fragmented reads;
- `mbedtls_base64_encode()` and `mbedtls_base64_decode()` for audio
  conversion;
- ESP-IDF's cJSON component for JSON parsing and type validation;
- `xRingbufferCreateWithCaps()` for the PSRAM capture ring.

Do not write custom WebSocket framing, base64, JSON, or ring-buffer
implementations, and do not duplicate the RFC 6455 upgrade in the AI client.
ESP-IDF 5.5 formats the authority as `<host>:<port>`, including default port
443. The assistant ingress therefore applies a scoped Traefik headers
middleware before forward-auth, rewriting both `Host` and
`X-Forwarded-Host` to the configured canonical external host. The second
header is required because the shared ForwardAuth has `trustForwardHeader=true`
and therefore preserves an existing forwarded host in preference to deriving
one from the rewritten request host. Routing and TLS selection have already
completed at that point, and Authentik receives the authority that matches its
`forward_single` configuration.

Initialize cJSON once with allocation hooks backed by
`heap_caps_malloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` and
`heap_caps_free()`. Parse bounded messages with
`cJSON_ParseWithLengthOpts()`, validate the required types and fields, and
delete the tree immediately after handling the event. This intentionally trades
a temporary PSRAM copy of a base64 string for a smaller, well-tested
implementation; it does not consume the scarce internal-memory margin.

Encode the three fixed outbound event shapes into one reusable PSRAM buffer.
Use `mbedtls_base64_encode()` directly into the reserved `audio` field rather
than constructing a second encoded string or asking cJSON to allocate a printed
document.

Decode response audio with `mbedtls_base64_decode()` into a reusable PSRAM PCM
buffer, then write it to I2S in bounded blocks. Both encoded and decoded sizes
must be derived with checked arithmetic before the decode call.

Unknown transcription and response-text event types are ignored after basic
JSON validation. They may increment the transport's unknown-event metric, but
are not logged per event at normal level.

### Playback

The speaker is owned only by the assistant session while assistant mode is
active.

- Accept the current 24 kHz `pcm16`, sample width 2, one-channel contract.
- Treat a changed format within one response as a protocol error.
- Convert 24 kHz to supported 32 kHz with a stateful ESP-DSP 4:3 rational FIR;
  do not feed the unsupported source clock to MAX98357.
- Enqueue converted PCM in a bounded PSRAM ring from the network task and let a
  dedicated playback task perform bounded I2S writes. Blocking I2S writes must
  never pause WebSocket reception.
- Start playback only after the configurable preroll. Count software queue
  starvation, queue timeout/overflow, and I2S short writes, and abort on data
  loss or a persistent playback failure.
- Keep the I2S TX channel enabled with idle DMA auto-clear so BCLK/LRCLK and
  digital silence exist before playback. The explicit zero-write primer is a
  bounded diagnostic control, not the onset fix; hardware comparison showed it
  is equivalent to the already-continuous old loopback idle path.
- Before the first response write, use audio-tools' standard popping-sound
  remover to hold PCM at zero until its first zero crossing. Report the input
  edge, retained edge, and suppressed sample count.
- Keep an optional saturating response gain in config, defaulting to 1.0, so
  speaker level can be tuned without changing protocol code.

Stereo can be added later if the service actually emits it. The current server
reports mono, and silently discarding or misinterpreting a channel is worse than
rejecting an unexpected format.

## Buffering and backpressure

Two bounded queues isolate the timing domains: capture cannot block the AFE
callback, and playback cannot block WebSocket reception.

Current bounded values:

- capture ring: 4 seconds of 16 kHz mono PCM16, 128,000 bytes in PSRAM;
- raw PCM per append: 2,048 bytes, or 64 ms;
- resulting JSON text message: about 2.8 KiB, below the current 4 KiB mbedTLS
  output limit;
- maximum received text event: 128 KiB in PSRAM;
- decoded response buffer: at most 96 KiB in PSRAM for a maximum-size audio
  event;
- converted playback ring: 512 KiB in PSRAM;
- playback preroll: 8 KiB, or 128 ms of 32 kHz mono PCM16;
- explicit priming write: 512 zero bytes in PSRAM, repeated from request commit
  until the server announces response audio;
- temporary cJSON allocations: PSRAM only and released after each event;
- all socket waits bounded and owned by the network task;
- all I2S writes bounded and owned by the playback task.

Every size remains configurable and validated. The append size must be even,
must fit the outbound event buffer after base64 expansion, and must not exceed
the active TLS/WebSocket write capacity.

If the capture ring fills, abort the turn and report overflow. Never drop an
arbitrary PCM region and continue with a corrupted command. Likewise, reject an
oversized response event instead of allocating opportunistically. The playback
producer may block briefly on a full queue because the consumer continues on a
separate task; a bounded send timeout aborts instead of dropping response PCM.

## Configuration

Add validated AI-local structs for:

- host, port, path, secure mode, root PEM, and authorization-header secret
  name;
- input PCM description;
- capture-ring duration/capacity and append PCM bytes;
- maximum inbound event bytes and derived decoded-audio capacity;
- connect, write, read-poll, response, and close timeouts;
- SNTP server, time-sync timeout, and minimum acceptable epoch;
- network task priority, core, stack size, and allocation policy;
- playback task priority, core, stack size, queue capacity, preroll, and wait
  timeouts;
- exact response and hardware playback PCM descriptions;
- response gain;
- dim-white `Listening`, amber `Recording`, black `Off`, and purple `Playback`
  status definitions;
- existing no-speech and maximum-command timeouts.

Validation must also enforce:

- NVS key length at most 15 characters;
- authorization-header capacity large enough for the generated Basic value;
- the loaded header value is non-empty, bounded, and contains no CR/LF/NUL;
- secure mode requires a trust root;
- assistant input is 16 kHz, PCM16, mono;
- buffer durations cannot overflow byte calculations;
- response and close timeouts are longer than individual socket waits.

Detector, VAD, AGC, and session endpoint tuning remain in their existing
configuration structs. The transport must not duplicate or override those
values.

## TLS and security

The existing AI SDK enables TLS and WebSocket transport, but three changes are
required before calling the path production-ready:

1. Certificate time validation
   - The generated AI SDK config currently has
     `CONFIG_MBEDTLS_HAVE_TIME_DATE` disabled.
   - Start SNTP after WiFi receives an address and require a plausible synced
     clock before WSS.
   - Enable date validation for the AI target only.
   - SNTP failure must not stop the AFE or WakeNet task; it makes assistant
     turns fail cleanly until time becomes valid.

2. Memory placement
   - Default mbedTLS currently allocates its 16 KiB input buffer, 4 KiB output
     buffer, and TLS state from internal memory.
   - Move mbedTLS allocations to PSRAM for the AI target, then measure handshake
     and steady-state low water.
   - Keep socket/DMA-required memory internal and retain a safe internal reserve.

3. Canonical HTTP authority
   - ESP-IDF 5.5's WebSocket upgrade always includes the numeric port in
     `Host`, even when it is the default TLS port.
   - Keep the client on ESP-IDF's complete WebSocket implementation.
   - Normalize `Host` and `X-Forwarded-Host` to `hal.d-reis.com` in an
     assistant-only Traefik headers middleware ordered before forward-auth. Do
     not broaden this behavior to unrelated ingresses.

Embed the Let's Encrypt trust root, not the current leaf or intermediate
certificate, and verify the live chain during implementation. Hostname/SNI must
remain `hal.d-reis.com`.

ESP-IDF's `transport_ws` implementation can print the complete Authorization
header value at debug level. Force that native tag to INFO or quieter before
setting credentials and never enable credential-bearing transport debug logs
during validation.

No credential may be compiled into the image, included in a filesystem image,
reported through metrics, or printed in an error message.

## Logging and metrics

Normal logs should cover state changes rather than audio frames:

- wake accepted or ignored because a turn is busy;
- recording opened and its VAD/safety endpoint;
- WiFi/time readiness failure;
- WSS connect/upgrade result and duration;
- input committed with PCM byte count and duration;
- first response audio with its announced format and latency;
- response audio/done and normal close;
- turn abort reason;
- return to wake-ready.

Do not log unchanged VAD/WakeNet results, append events, audio deltas, base64,
transcripts, or the authorization header value.

Keep the existing wake/session metrics and add a compact assistant transport
group covering:

- turns started, committed, completed, and aborted by reason;
- busy-state wake detections;
- connect attempts/successes/failures and last handshake status;
- raw PCM bytes captured/sent and response PCM bytes played;
- capture high-water and overflows;
- text frames sent/received and unknown events;
- JSON, base64, format, and close errors;
- socket read/write failures and timeouts;
- I2S short writes;
- playback queue starvation/underflow and send-timeout/overflow counters;
- playback queue depth/high-water, maximum empty wait, and maximum blocking
  I2S write duration;
- connect latency, commit-to-first-audio latency, response duration, and total
  turn duration;
- current turn state as a gauge.

Prewarm all metric handles during setup before the AFE or network task can
touch them.

## Implementation phases

### Phase 1: protocol and TLS client

1. Apply the breaking shared `AudioSink` authorization-header rename and make
   the secret value pass through unchanged; remove every old Bearer-specific
   identifier and behavior.
2. Add the AI-local config and WSS transport wrapper.
3. Add ESP-IDF's `json` component to the AI target dependencies and use the
   existing `tcp_transport`, `esp_ringbuf`, and `mbedtls` components.
4. Load the complete Authorization header value from `hal-auth` during boot
   and reuse it for turns. Do not access NVS from the PSRAM-stack network task,
   because NVS temporarily disables the flash cache.
5. Add root validation, SNTP readiness, and AI-only mbedTLS memory settings.
   Keep mbedTLS PEM parsing enabled because the configured trust anchor is PEM;
   otherwise mbedTLS treats it as DER and rejects it before the handshake.
   Keep ESP-IDF's dynamically allocated WebSocket upgrade buffer at 4 KiB so
   the forward-auth response headers fit; it is freed after the handshake.
6. Implement bounded text send/receive, fragmentation handling, close handling,
   and the narrow cJSON/mbedTLS-base64 codec on the standard ESP-IDF transport.
7. Exercise the codec with captured service event fixtures before hardware use.

### Phase 2: turn capture

1. Remove the delayed loopback, its configuration, storage, and metrics.
2. Add the four-state session and a cap-aware ESP-IDF ring buffer in PSRAM.
3. Start a connection on wake without blocking the AFE callback.
4. Send continuous cleaned PCM through the existing post-wake VAD endpoint.
5. Commit exactly once on a valid command endpoint; abort no-speech turns.
6. Preserve WakeNet independence from VAD and all existing safety exits.

### Phase 3: response playback

1. Validate `response.audio.started` and reconfigure MAX98357 output.
2. Start explicit zero-PCM I2S writes after request commit, decode audio deltas
   into the bounded playback ring, then hand off from silence to response PCM
   without stopping I2S or backpressuring the socket.
3. Add the purple `Playback` status and response gain; restore dim-white
   `Listening` after every completion or abort.
4. Return directly to wake-ready after response/audio completion and normal
   close, with no `Draining` state.
5. Verify speaker output is never routed back into the active upload turn.

### Phase 4: hardening and documentation

1. Add concise logs, metrics, timeout/error cleanup, and repeated-turn support.
2. Update `docs/audio.md` with the implemented path and measured formats.
3. Update `docs/commands.md` with credential provisioning and validation
   commands.
4. Record final RAM, PSRAM, stack, CPU, capture high-water, and latency values.

The required Kubernetes changes are deliberately narrow: correct the generated
Basic username at the Authentik service-account source and normalize the
assistant ingress authority before forward-auth. No assistant-service code
change is required. If real TTS delta events exceed the bounded client event
size, prefer adding explicit server-side delta chunking in a separately
reviewed change rather than increasing embedded buffers without a limit.

## Validation

Build checks:

~~~text
bin/build -e ai
bin/build -e media
~~~

The media build is a regression check for any shared AudioSink/I2S header
changes. A normal AI upload still includes the existing ESP-SR model image.

Static cleanup checks:

~~~text
rg 'bearerTokenSecretName|hasBearerToken|webSocketMaxBearerTokenBytes' include src
rg '_prepareAuth|_authHeader' include src
rg 'DelayedPlayback|delayedPlaybackConfig|max98357LoopbackSinkConfig' src/ai
rg 'fwdB|zeroB|playSamp|sinkDrop' src/ai
~~~

Both searches must return no matches. A focused transport test must also seed a
secret with `Basic test-value` and verify that the exact value reaches the
generic sink and assistant ESP-IDF transport configuration without prefixing,
stripping, or scheme parsing.

Clean-boot checks:

1. Provision WiFi and `hal-auth`, flash the AI node, and capture a reset.
2. Confirm WiFi, SNTP, AFE, models, I2S, and assistant task initialize cleanly.
3. Confirm the node transitions from cyan `Booting` through blue `CoreReady` to
   dim-white `Listening`, without showing green `TargetsReady`.
4. Confirm the idle node does not hold an assistant WebSocket open.
5. Inspect `/metrics` and `/monitor` before a turn.

Functional hardware checks:

1. Speak without either wake word. No socket opens and no audio is played.
2. Say `Alexa` followed immediately by a command. Recording turns amber at
   wake, including while WSS connects, and `session.update` selects
   `attenborough`.
3. Say `Computer` followed immediately by a command and confirm
   `session.update` instead selects `bender`.
4. Confirm PCM capture begins at the wake frame, no ring overflow occurs, and
   VAD ends the command.
5. Confirm Recording resets at the VAD endpoint, the LED turns off, the final
   PCM tail is sent, and exactly one commit is logged.
6. Confirm the server announces 24 kHz mono PCM16, the device reports the
   24-to-32 kHz conversion, streamed audio plays intelligibly, the LED is
   purple during playback, `pbUnder`, `pbOver`, and `shortWr` remain zero, and
   no local microphone loopback or onset crack is heard.
7. Confirm microphone upload bytes do not increase during response playback.
7. Confirm `response.audio.done`, `response.done`, and a normal close return
   the node to wake-ready without a `Draining` status.
8. Repeat several turns without rebooting.
9. Say only the wake word and remain silent. The no-speech timeout must abort
   without committing.

Failure checks:

- missing/wrong authorization value;
- invalid clock or SNTP unavailable;
- WiFi loss before connect, during upload, and during response;
- server connect/response timeout;
- capture overflow;
- oversized/invalid JSON or base64;
- unexpected response audio format;
- abnormal WebSocket close;
- I2S short write.
- playback queue starvation or overflow.

Every failure must leave the AFE running, clear the per-turn status handles,
restore `Listening`, release all per-turn resources, and permit a later wake to
start a fresh connection.

Resource checks during a real TLS turn:

- no allocation failure, watchdog, AFE feed/fetch failure, or source starvation;
- capture ring remains below capacity;
- network and AFE task stack high-water values retain a useful margin;
- internal-memory low water remains safe for WiFi/I2S DMA work;
- PSRAM returns to a stable level across repeated turns;
- core-1 AFE utilization and ring headroom do not regress materially.

The working desktop reference remains:

~~~text
~/.local/gitbin/ai assist
~~~

It is useful for comparing service event order and response audio independently
of the embedded implementation.

## Preliminary implementation record

Validated on the connected AI node on 2026-07-14:

- `bin/build -e ai` succeeds at 117,804 bytes of static RAM and 1,707,143
  bytes of application flash. `bin/build -e media` is the shared
  AudioSink/I2S/StatusLed regression build.
- The complete AI firmware and ESP-SR model image flashes successfully. A reset
  loads `vadnet1_medium` and `wn9_alexa`, reports the expected
  NS -> VAD -> WakeNet -> AGC pipeline, starts the assistant task on core 0,
  and leaves the AFE running on core 1.
- The live `hal.d-reis.com` certificate is valid from 2026-07-13 through
  2026-10-11. Its served Let's Encrypt YR1/Root YR chain validates to the
  embedded ISRG Root X1 trust anchor.
- WSS readiness now requires an SNTP event from the current boot. The final
  reset logs `clock=waiting`, starts SNTP only after the station receives an
  address, logs `Assistant SNTP synchronized`, and then reports both WiFi and
  clock ready. A warm-reset-retained plausible clock is not sufficient.
- The idle node holds no assistant connection open. The `aiAsst` counters all
  remain zero, AFE failures remain zero, and AFE ring headroom is 98%.
- Before the infrastructure correction, the node's 114-byte `hal-auth` value
  matched the then-live `assistant-esp32-auth` Kubernetes Secret and Vault
  record, and a boolean-only comparison confirmed that the password matched the
  live Authentik token key. This isolated the issue to credential semantics,
  not drift. The node's secret is sealed and remains intentionally untouched;
  the final clean boot passes the non-logging authorization-header preflight.
- The first spoken wake test exposed an ESP-IDF cache-safety assertion. Its
  decoded backtrace led from `AssistantWebSocket::connect()` through the secret
  service into an NVS flash read while the assistant task was using its PSRAM
  stack. Authorization is now loaded during boot on the internal setup stack
  and reused by each turn; the patched full image builds, flashes, and reaches
  network readiness without moving the 12 KiB task stack into scarce internal
  memory.
- Subsequent spoken tests exposed two AI sdkconfig limits before an HTTP
  upgrade could complete. mbedTLS PEM parsing had inherited the shared disabled
  default and rejected the PEM trust anchor with `-0x2180`; the ESP-IDF
  WebSocket transport then exhausted its default 1 KiB response-header buffer.
  The AI override now enables PEM parsing and configures ESP-IDF's dynamically
  allocated WebSocket buffer at 4 KiB. It is large enough for the observed
  forward-auth headers and is released after the upgrade.
- The remaining authentication failure was semantic, not state drift. The live
  token is active, non-expiring, has intent `app_password`, and belongs to the
  active `assistant-esp32` service account. The generated Basic header instead
  uses the reserved username `goauthentik.io/token`, which tells Authentik to
  evaluate its password as a bearer token. A prior direct bearer probe's
  `Token invalid/expired` response was therefore expected and was not evidence
  of a mismatched key.
- Secret-safe live probes isolate both independent protocol errors: the stored
  header with canonical `Host: hal.d-reis.com` returns HTTP 302; the same app
  password paired with the real `assistant-esp32` username returns HTTP 101;
  and that corrected credential with `Host: hal.d-reis.com:443` returns HTTP
  404. The infrastructure header must therefore encode the real service-account
  username with the app password and must canonicalize the authority before
  forward-auth.
- The source fixes are localized in `../../k8s`: the service-account Secret's
  `username` and encoded Basic username now use the actual Authentik username,
  while the app-password token remains the password. The assistant values add
  a namespaced `canonical-host` Traefik headers middleware before the existing
  forward-auth middleware. It sets both `Host` and `X-Forwarded-Host` because
  the latter is trusted and preferred by the shared ForwardAuth middleware.
  Terraform deployment regenerated the live Kubernetes Secret with the actual
  username. Device credential rotation is a separate provisioning operation
  and is not performed by this implementation.
- ESP-IDF 5.5 and current upstream always append the port to the WebSocket
  `Host` header. A diagnostic custom RFC 6455 upgrade proved that omitting the
  port passes the proxy, but duplicating the SDK handshake was rejected as
  disproportionate and removed. The final firmware uses ESP-IDF for the entire
  upgrade and frame lifecycle; the scoped ingress middleware absorbs the
  authority-format difference.
- A live secret-safe probe using the regenerated Basic header and the exact
  ESP-IDF authority `Host: hal.d-reis.com:443` returns HTTP 101 after both host
  headers are normalized. The same probe returned Authentik HTTP 404 when only
  `Host` was rewritten, confirming that routing already succeeded and the
  trusted forwarded host was the remaining mismatch.
- AI no longer selects the generic green `TargetsReady` state. Setup promotes
  the configurable dim-white `Listening` state, WakeNet's amber `Recording`
  warning temporarily overrides it, the VAD endpoint selects black `Off`,
  server audio promotes purple `Playback`, and cleanup restores `Listening`.
  Informational states are exclusive; higher severity state masks retain their
  existing cycling behavior.
- A post-flash spoken wake validates the stock ESP-IDF client through SNTP,
  WakeNet, post-wake VAD, capture close, TLS, and an HTTP response. The sealed,
  pre-rotation device header receives a non-101 Authentik response consistent
  with the earlier HTTP 302 probe, so ESP-IDF correctly reports the missing
  `Sec-WebSocket-Accept` and the turn returns cleanly to wake readiness without
  a crash. This is now a credential-provisioning boundary rather than a
  firmware transport blocker.
- `/monitor` reports 20,407 bytes internal-data free with a 19,151-byte low
  water, and 7,257,812 bytes PSRAM free with a 7,256,080-byte low water. The
  idle assistant task uses about 0.04% CPU and retains 10,288 bytes of its
  12,288-byte PSRAM stack. The AFE uses about 31% of core 1.
- No crash, abort, watchdog, capture overflow, status failure, or I2S short
  write occurred during the clean-boot and idle observation.
- Static cleanup finds no delayed-loopback identifiers or old Bearer-specific
  authorization API. The generic WebSocket sink and the assistant client both
  pass the configured header value through unchanged.
- After device provisioning, a timestamped spoken turn completed end to end:
  WakeNet and post-wake VAD captured 56,320 bytes, the service received the
  same PCM count, PocketTTS produced 134,400 response bytes, the device decoded
  and wrote the same count, and the server closed normally with code 1000.
  There were no short writes, transport gaps, or byte loss.
- That exact-byte trace isolated the remaining distortion to the hardware
  format boundary: MAX98357 explicitly does not support PocketTTS's 24 kHz
  LRCLK. Playback now uses a stateful ESP-DSP 4:3 FIR to produce supported
  32 kHz PCM while retaining the 24 kHz source bandwidth.
- Person-present replay showed that conversion improved the sound but did not
  remove holes inside words. Cross-service timestamps then showed PocketTTS
  generating at 1.9--2.9 times realtime while synchronous device I2S writes
  paused WebSocket reads and delayed subsequent server sends. Playback is now
  isolated behind a 512 KiB PSRAM ring with a 64 KiB preroll and a dedicated
  consumer task. The build uses 124,276 bytes of static RAM and 1,728,931 bytes
  of application flash.
- The final flashed queue build boots cleanly with 19,539 bytes of internal-data
  memory free and a 17,399-byte low water. After the 512 KiB runtime allocation,
  default allocatable memory reports 6,705,784 bytes free with a 6,651,184-byte
  low water. Idle `AiPlayback` CPU rounds to 0.00% and its 4,096-byte PSRAM stack
  retains 2,140 bytes; `AiAssistant` retains 8,948 bytes of its 12,288-byte
  stack. The AFE remains isolated on core 1 at about 35% CPU.
- A 10 ms first-sample fade was tested after the queue change. Three consecutive
  turns completed normally and played 583,680 output bytes with zero queue
  underflows, queue overflows, or I2S short writes, but the listener still heard
  the onset transient in a softened `pffff` form. This disproved the assumption
  that a plain amplitude ramp is sufficient. The fade was removed.
- Explicit 512-byte zero writes from request commit through the preroll handoff
  also left the transient unchanged. The earlier one-second local loopback is
  the decisive comparison: it continuously wrote zero frames before switching
  to delayed microphone PCM and produced the same onset sound. Together with
  the TX channel being enabled at boot with `auto_clear`, this excludes
  I2S/MAX98357 startup and isolates the shared zero-to-arbitrary-PCM boundary.
- The final PCM writer now applies audio-tools' standard
  `PoppingSoundRemover` until the first waveform zero crossing, before response
  gain and I2S. `pbEdgeIn`, `pbEdgeOut`, and `pbMute` expose the input boundary,
  retained boundary, and number of suppressed samples. The flashed build uses
  124,340 bytes of static RAM and 1,736,591 bytes of application flash and
  reaches true Listening readiness cleanly; subjective onset validation remains
  person-present.
- The latency pass keeps the 2 KiB streaming append cadence but enables
  `TCP_NODELAY`, moves the AI-only WiFi/lwIP configuration from minimum-memory
  portal values to AMPDU plus a 1440-byte MSS and 11,520-byte TCP windows,
  lowers VAD endpoint silence from 800 ms to 320 ms, and lowers response preroll
  from 64 KiB to 8 KiB. Priming writes now stop when response audio is announced
  so queued silence can drain before the first nonzero write. Commit logs expose
  exact endpoint-to-commit, final-drain, append-send, and commit-write timing,
  and the first nonzero I2S write has its own announcement-relative checkpoint.
  The latency build succeeds at 124,356 bytes of static RAM and 1,738,915 bytes
  of application flash, uploads successfully, and reaches Listening with 19,967
  bytes of free internal data memory.
- Two person-present responses completed normally with a 26 ms device-timestamp
  gap from endpoint to commit and 40--52 ms from audio announcement to the first
  nonzero I2S write. Active-turn memory retained 17,223 internal bytes and
  6,648,080 external bytes. The first response had no playback starvation. A
  6.08-second second response arrived over 7.122 seconds and produced five
  starvation intervals, a 1.116-second maximum producer wait, and a later
  199,808-byte queue high-water. Playback now preserves the fast 8 KiB initial
  onset but, after starvation, waits for 8 KiB to rebuild or for producer
  completion before resuming. A similarly bursty response is still required to
  exercise that recovery branch on hardware.
- The 2026-07-27 multi-wake build adds `wn9_computer_tts` beside
  `wn9_alexa`, latches a profile from the reported WakeNet model/word indices,
  and sends `voice=attenborough` for Alexa or `voice=bender` for Computer in
  `session.update`. It builds at 124,428 bytes of static RAM and 1,745,039 bytes
  of application flash. The complete 870,132-byte model bundle and firmware
  uploaded successfully. A reset loaded both WakeNet models, reached Listening,
  retained 12,867 bytes of internal data memory and 6,368,876 bytes of external
  memory, and ran the AFE at about 40--42% of core 1 with 98% ring headroom and
  no AFE failures. Spoken verification of both profile selections remains
  person-present.

The remaining validation boundaries are the listener's subjective confirmation
of the response onset and spoken verification of both wake-profile voice
selections. Protocol completion, byte integrity, LED sequencing, allocation,
queue behavior, repeated turns, and clean boot are already exercised.

## Acceptance criteria

The phase is complete when:

- wake-word detection remains independent of VAD;
- Alexa selects `attenborough` and Computer selects `bender` in the per-turn
  `session.update`;
- a wake-started command reaches `hal.d-reis.com` as ordered 16 kHz PCM16
  append events followed by one commit;
- the complete `Basic ...` Authorization header value is read from the renamed
  secret configuration and passed unchanged, with no old Bearer API remaining;
- the Authentik machine credential and Let's Encrypt certificate are validated
  without secrets in logs;
- announced 24 kHz remote PCM is converted to supported 32 kHz and plays
  intelligibly through MAX98357;
- no microphone audio is uploaded while the server response is playing;
- completion and every tested error re-arm WakeNet without rebooting;
- no delayed loopback code, configuration, storage, routing, or loopback-only
  metrics remain;
- there is no production `Draining` state;
- repeated-turn metrics show no drops, leaks, watchdogs, or unsafe memory
  pressure.

## Non-goals

- Multi-turn conversational history on one WebSocket.
- Barge-in or cancelling response playback with a new wake word.
- Simultaneous microphone upload and speaker playback.
- Acoustic echo cancellation.
- Opus, WAV, or binary assistant frames.
- Displaying or logging transcripts.
- Retaining the local delayed loopback or its diagnostic scaffolding.
- Backward-compatible aliases or migration behavior for Bearer-specific
  `AudioSink` configuration names.
- Generalizing the assistant protocol into `AudioSink::WebSocketSink`.
- Changing the assistant service or Kubernetes deployment unless bounded
  server-side response chunking proves necessary.
