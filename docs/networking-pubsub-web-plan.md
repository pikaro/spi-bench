# Networking, PubSub UDP, And Web Interface Plan

This document records the first-pass feasibility review for adding WiFi,
socket participation, UDP PubSub transport, host tooling, and a later web
interface. It is intentionally a planning note, not an implementation contract.

## Current Context

- At original drafting time, the project had no active WiFi, socket, HTTP
  server, or WebSocket implementation. Phase 1 has since added and validated
  master WiFi/socket participation; HTTP/WebSocket remains later scope.
- The master node is the intended owner of WiFi and central coordination.
- The active firmware shape is static-oriented, modular, and based on small
  components with `Facade.hpp`, optional `Interfaces/`, and `detail/`
  implementation boundaries.
- ESP-IDF WiFi, lwIP, and HTTP server internals will allocate and create their
  own tasks. Static-only should mean project-owned queues, buffers, slots, and
  task configuration stay bounded and explicit.
- `docs/wifi.md` contains access data. Do not duplicate the credentials in
  generated planning docs or logs.
- `legacy/include` is a symlink to the previous project include tree. The old
  WiFi and web code is useful as an ESP-IDF call reference, but it does not
  match the new component model.

Relevant legacy files:

- `legacy/include/Wifi/WifiAccessPoint.hh`
- `legacy/include/WebServer/WebServerBase.hh`
- `legacy/include/WebServer/WebSocketServer.hh`

## Global Assumptions

Blocking assumptions:

- WiFi, HTTP, WebSocket, and browser-facing PubSub views run on the master or a
  host tool, not on GPU nodes.
- Browser participation uses WebSocket. Browsers cannot directly use UDP or raw
  TCP sockets.
- Phase 2 UDP PubSub is limited to one UDP peer. Multi-peer UDP routing is a
  later design problem.
- `PLATFORM_TEST` is reserved for unit tests. Host-side operation should use a
  separate `PLATFORM_HOST` target/platform layer.

Non-blocking assumptions:

- WiFi station and AP credentials can start in ignored master-local static
  config, with tracked example structure only. NVS-backed storage/provisioning
  can be added later if needed.
- UDP initially uses one learned peer: the MCU listens on UDP port 2026 and the
  first valid sender becomes the active peer. No NAT traversal, broadcast fanout,
  or multi-peer endpoint routing is part of Phase 2.
- Host-originated PubSub frames may set `timestampUs = 0` until host/device time
  synchronization is deliberately implemented.

## Time Model

The current Clock service synchronizes MCU-local monotonic clocks. It does not
provide wall-clock time. PubSub uses synced microsecond timestamps primarily for
latency measurement and diagnostics.

Important current behavior:

- Application envelopes normally require a synced clock.
- Control-plane PubSub messages may be emitted before sync.
- `timestampUs = 0` already means "no comparable synced timestamp".

For the first host/UDP participant:

- Add a single host node identity only when the host needs to publish as a real
  PubSub source.
- A reasonable first value is `NodeId::Host = 1U << 7`, which consumes the last
  bit in the current `uint8_t` one-hot node ID space.
- Treat this as an explicit single-host limitation, not a general host-peer
  expansion.
- Host-originated packets should use `timestampUs = 0` until a host/device sync
  protocol exists.
- Receivers must avoid latency calculations when `timestampUs == 0`.

Do not implement NTP as the first solution. It is useful later for wall-clock
correlation, but it does not automatically match the current monotonic clock
sync semantics. When a future monotonic wall-clock IC becomes the master's
authoritative time source, revisit time sync. A small UDP variant of the
existing Clock exchange may be a better fit for host/device latency alignment
than generic NTP.

## Phase 1: WiFi And Basic Network Participation

Goal:

- Add master-owned WiFi Access Point support.
- Add WiFi station/client support using credentials supplied through ignored
  master-local config.
- Provide a common network facade for code that only needs "network ready" and
  socket participation.
- Add basic UDP and TCP helpers for bounded, convenient network participation.

Expected implementation shape:

- `include/Wifi/Facade.hpp`
- `include/Wifi/Interfaces/Config.hpp`
- `include/Wifi/Interfaces/Types.hpp`
- `include/Wifi/detail/Wifi.hpp`
- `include/Wifi/detail/PlatformSelect.hpp`
- `include/Wifi/detail/platform/PlatformESP32.hpp`
- `include/Network/Facade.hpp`
- `include/Network/Interfaces/Config.hpp`
- `include/Network/Interfaces/Types.hpp`
- `include/Network/detail/platform/PlatformESP32.hpp`
- `include/StaticConfig/Wifi.hpp`
- `include/StaticConfig/Network.hpp`

Build/config work:

- Enable `ENABLE_WIFI=ON` only for the master environment at first.
- Keep WiFi/lwIP/http Kconfig enablement scoped to the environment that needs
  it.
- Do not add new persistent PlatformIO environments.
- Use constexpr project configuration in `include/StaticConfig/` for ordinary
  project-owned knobs.
- Leave true platform selection in CMake/Kconfig where ESP-IDF requires it.

Lifecycle plan:

1. Add minimal WiFi AP and STA config structs.
2. Add an ESP32 WiFi component that owns `esp_netif_init`,
   `esp_event_loop_create_default`, `esp_wifi_init`, mode selection, and event
   handlers.
3. Support AP-only and STA-only modes. Keep AP+STA as a later extension because
   it needs explicit channel and reconnect policy.
4. Expose readiness as small status methods rather than a broad abstraction:
   initialized, started, connected, got IPv4, AP active.
5. Add a small network facade around UDP/TCP sockets using lwIP sockets.
6. Keep socket buffers and receive/send packet slots bounded.
7. Add metrics/logs for connection state, socket errors, RX drops, TX failures,
   and reconnect attempts.
8. Integrate setup only in `src/master/main.cpp`.
9. Verify with `bin/build -e master`; if shared platform/config headers change,
   build `master`, `media`, `gpu0`, `gpu1`, and `io`.

Phase 1 risks:

- Enabling WiFi pulls in lwIP and ESP-IDF WiFi/coexistence components, increasing
  flash/RAM usage.
- ESP-IDF owns internal tasks and heap allocations. Project-owned code should
  still keep bounded storage.
- AP+STA mode still needs explicit channel and reconnect behavior before it can
  be added safely.
- WiFi startup order must not break existing console, logging, filesystem, or
  PubSub setup.
- Same-network UDP/TCP validation depends on the host and MCU actually sharing a
  routable subnet. Current host-on-guest validation proves TCP in both
  directions and UDP replies to MCU-originated flows, but unsolicited
  host-to-MCU UDP still times out on the guest WiFi. Phase 2 treats that
  host-to-MCU timeout as a blocker because the MCU learns the active peer from
  the first valid sender to UDP port 2026.

## Phase 2: UDP PubSub And Host Participant

Working implementation plan: `docs/networking-phase2-implementation-plan.md`.

Goal:

- Add a single-peer UDP transport for PubSub.
- Let the device send PubSub frames to one UDP peer on the same WiFi network.
- Let the device receive PubSub frames from that UDP peer.
- Build host-side C++ PubSub components for send/receive operation.
- Add a Python notify bridge with async per-type handlers and Pydantic payload
  models.
- Start with a ship-bell/button handler that logs one line, while keeping the
  host trusted for arbitrary generated PubSub publish/subscribe.

UDP transport direction:

- Model initial UDP as `PointToPoint`, not `SharedBusRouter`.
- The UDP transport represents one remote routing domain.
- The MCU listens on UDP port 2026 and learns the active peer from the first
  valid sender to that port.
- The transport sends periodic keepalives and resets peer state when the learned
  peer goes stale.
- Multi-peer subscription tracking by IP endpoint is out of scope.

PubSub fit:

- `ITransport` already provides the needed shape: enqueue serialized frames,
  receive raw frames, poll ingress, and support direct raw forwarding.
- `BaseTransport` can likely be reused if the UDP link exposes send/receive
  callbacks with bounded packet storage.
- UDP socket receive should run in its own bounded transport task; PubSub should
  drain already-received frames instead of blocking on socket receive.
- PubSub's current one-hot peer model is not a good fit for arbitrary IP
  endpoint fanout. Avoid expanding it for Phase 2.

Host build direction:

- Add `PLATFORM_HOST`, separate from `PLATFORM_TEST`.
- Keep `PLATFORM_TEST` for future unit tests.
- Host operation needs enough platform shims for error/log/CRC/time paths used
  by `Codec` and `SerDe`, or a small host-safe split around the serialization
  code.
- Python package dependencies are acceptable when useful for the host bridge.
  Pydantic should be used for Python-facing wire payload models.

Python participant direction:

- Use C++ host PubSub code for UDP send/receive and frame encode/decode.
- Bridge decoded PubSub messages into async Python handlers by generated message
  type.
- Represent packets with `timestampUs = 0` until time sync exists.
- Start with a ship-bell/button subscription handler that emits a log line.
- Keep the host trusted: arbitrary generated PubSub messages should be possible
  from the host.

Phase 2 risks:

- Host `NodeId` consumes the last currently available one-hot bit if added as
  `1U << 7`.
- Host-side compilation may reveal ESP-IDF assumptions in headers that need
  small platform splits.
- UDP has no delivery guarantee. That matches noncritical PubSub traffic but
  should be explicit for control-plane expectations.
- Subscription replay over UDP should be tested carefully because peer
  availability is less concrete than SPI/RS485 readiness.
- Host-to-MCU UDP timeout blocks Phase 2 because peer learning depends on a host
  datagram reaching MCU port 2026.

## Phase 3: HTTP, WebSocket, JavaScript Models, And Web PubSub

Goal:

- Add a master-side HTTP server.
- Add a bounded WebSocket server.
- Add JavaScript SerDe/Codec support.
- Extend `make wire` to generate JS models.
- Add a JavaScript PubSub participant.
- Build a web interface for observing PubSub data and sending PubSub events.

HTTP/WebSocket direction:

- Rewrite rather than port the legacy web server.
- Use ESP-IDF `esp_http_server`.
- Serve static files from LittleFS in chunks.
- Use fixed route tables and fixed WebSocket client slots.
- Avoid `std::unordered_map`, broad `std::function` use, per-message heap
  allocation, and nanopb/proto coupling from the legacy implementation.
- Keep this master-side or host-side. Do not put a user-facing web interface on
  GPU nodes.

JavaScript wire generation direction:

- Extend the generator to emit field types, enum widths/values, array extents,
  topic IDs, and constants.
- The current C++ generator only emits field lists and field names, which is not
  enough for standalone JS encoding.
- JS Codec should match the existing little-endian scalar/enum/array format and
  CRC32 frame footer.

Web PubSub direction:

- Browser PubSub should ride over WebSocket.
- The device-side WebSocket component can bridge between WebSocket frames and
  local PubSub publish/subscribe.
- Browser-facing PubSub may need explicit topics/events later, separately from
  the trusted Phase 2 host bridge.

Phase 3 risks:

- WebSocket fanout can easily become heap-heavy or queue-heavy. Keep client and
  message storage fixed.
- Browser traffic must not become part of the LED hot path.
- The web UI should send compact PubSub events, not full LED frames.
- JS generation needs schema richness that the current generator does not yet
  extract.

## Suggested Implementation Order

1. Phase 1 WiFi AP/STA and basic UDP/TCP socket wrappers.
2. Minimal UDP echo/diagnostic command on master.
3. Single-peer UDP PubSub transport on MCU port 2026.
4. `PLATFORM_HOST` serialization build.
5. Host-side C++ PubSub peer with a Python/Pydantic notify bridge.
6. HTTP static server.
7. WebSocket server with bounded client/message slots.
8. JS SerDe/Codec generator.
9. Browser PubSub participant and web UI.

## Open Questions

- What exact C++/Python bridge technology should be used?
- What exact generated Pydantic model strategy should be used?
- Which browser-facing PubSub topics should be exposed later in Phase 3?
- What level of host/device time sync is needed after the monotonic wall-clock
  hardware is introduced?
