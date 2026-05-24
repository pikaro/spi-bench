# Phase 1 Networking Implementation Plan

This document is the working implementation plan for Phase 1 networking:
WiFi Access Point support, WiFi station/client support, a small common facade,
and basic UDP/TCP helpers.

It complements `docs/networking-pubsub-web-plan.md`. During implementation,
this document may be updated as details become concrete. If an update would
contradict the rough plan, pause and decide whether the rough plan also needs
to be revised.

## Scope

In scope:

- Master-owned WiFi bring-up.
- AP-only and STA-only operation. AP/STA combined mode is deliberately out of
  scope for this pass.
- A small facade for network readiness and local interface status.
- Basic UDP and TCP socket helpers for bounded on-device networking.
- Master-only build and setup integration.
- Logging for connection state and network/socket failures.
- A small host-side probe utility for UDP/TCP and route diagnostics.

Out of scope:

- UDP PubSub transport. That belongs to Phase 2.
- HTTP and WebSocket servers. Those belong to Phase 3.
- Browser participation.
- Host-side `PLATFORM_HOST` support.
- New provisioning UX.
- Multi-peer UDP routing.
- New persistent PlatformIO environments.
- NVS-backed credential provisioning.

## Design Constraints

- Keep project-owned storage bounded and explicit.
- Do not add project-owned build flags for ordinary configuration. Use
  `include/StaticConfig/` or local source config.
- Keep ESP-IDF/Kconfig/CMake switches only for true platform or component
  selection.
- Scope WiFi/lwIP enablement to the master environment first.
- Avoid heap-backed project structures in the socket convenience layer.
- Do not place WiFi, web, or socket helpers in GPU hot paths.
- Do not duplicate WiFi credentials in docs or logs.

ESP-IDF WiFi and lwIP will allocate internally and create internal tasks. That
is acceptable, but the project-owned wrapper should not hide those costs.

## Implemented Files

WiFi component:

- `include/Wifi/Facade.hpp`
- `include/Wifi/Interfaces/Config.hpp`
- `include/Wifi/Interfaces/Types.hpp`
- `include/Wifi/detail/Wifi.hpp`
- `include/Wifi/detail/PlatformSelect.hpp`
- `include/Wifi/detail/platform/PlatformESP32.hpp`
- `include/StaticConfig/Wifi.hpp`

Network/socket component:

- `include/Network/Facade.hpp`
- `include/Network/Interfaces/Config.hpp`
- `include/Network/Interfaces/Endpoint.hpp`
- `include/Network/detail/UdpSocket.hpp`
- `include/Network/detail/TcpSocket.hpp`
- `include/Network/detail/Commands.hpp`
- `include/Network/detail/PlatformSelect.hpp`
- `include/Network/detail/platform/PlatformESP32.hpp`
- `include/StaticConfig/Network.hpp`

Integration:

- `src/master/config.hpp`
- `src/master/main.cpp`
- `src/master/wifi_credentials.example.hpp`
- `src/master/wifi_credentials.hpp` as an ignored local file
- `bin/net-probe`
- `platformio.ini`
- `sdkconfig.stack.master` or the existing sdkconfig stack if a master-only
  override is not practical.
- `docs/overview.md` only if the active project shape changes materially.
- `docs/networking-phase1-implementation-plan.md` as implementation findings
  accumulate.

## Configuration Plan

`StaticConfig/Wifi.hpp` holds ordinary project-owned defaults:

- AP channel.
- AP max station count.
- STA reconnect behavior.
- WiFi memory/event defaults that are not credentials.

Credential handling needs care:

- `docs/wifi.md` is the current source of station access data.
- Do not copy credentials into this plan or other docs.
- Do not log passwords.
- First-pass firmware credentials live in ignored
  `src/master/wifi_credentials.hpp`.
- `src/master/wifi_credentials.example.hpp` documents the required shape with
  separate `Station` and `AccessPoint` structs so the two credential sets do not
  get confused.
- `src/master/config.hpp` falls back to disabled WiFi if the ignored local
  credentials header is absent.
- Runtime NVS-backed credentials are reasonable later, but they add provisioning,
  validation, erase/reset, and error-reporting behavior. Keep them out of this
  first implementation unless static local credentials become a blocker.

`StaticConfig/Network.hpp` holds bounded socket defaults:

- UDP RX packet bytes.
- TCP RX buffer bytes.
- TCP connect/send/receive timeouts.
- Maximum diagnostic command timeout.
- TCP listen backlog.

## WiFi Component Shape

Public surface should stay small:

- `begin(config)`
- `end()`
- `mode()`
- `started()`
- `staConnected()`
- `staHasIpv4()`
- `apStarted()`
- `apStationCount()`
- optional getters for local IPv4, gateway, and netmask

The component should own:

- `esp_netif_init`
- `esp_event_loop_create_default`
- default AP and STA netifs
- `esp_wifi_init`
- `esp_wifi_set_mode`
- `esp_wifi_set_config`
- `esp_wifi_start`
- WiFi/IP event handlers

Expected modes:

- `Disabled`
- `AccessPoint`
- `Station`

Implementation note: AP and STA are mutually exclusive for this pass. Combined
AP+STA would need explicit decisions for channel coupling, reconnect behavior,
and IP setup; leave it as a later feature if there is a concrete need.

## Network Socket Shape

The network component should be a thin lwIP socket wrapper, not a general
network framework.

Endpoint representation:

- IPv4 address as a 32-bit value or a small byte array.
- Port as `uint16_t`.
- Formatting helpers for logs only.

UDP helper:

- `open(bindPort)`
- `close()`
- `sendTo(endpoint, bytes)`
- `receiveFrom(buffer, timeoutMs)` returning endpoint plus length

TCP helper:

- client connect/send/receive/close
- one-client listener/accept/send/receive/close for diagnostics

The initial helpers can be synchronous wrappers with explicit timeouts. Add
managed tasks only when there is a real owner that needs background receive.
Diagnostic command timeouts are capped so console commands cannot hold the
command task long enough to trigger the watchdog.

## Logging And Metrics

The current implementation logs high-signal state transitions:

- WiFi initialized.
- AP started/stopped.
- STA connected/disconnected.
- STA got/lost IP.
- Socket open/close failures.

Do not log credentials.

Metrics were not added in this first pass. Useful future metric groups:

- WiFi core/state: AP starts, STA connects, disconnects, IP events, reconnect
  attempts.
- Network errors: socket open failures, bind failures, send failures, receive
  failures.
- UDP/TCP counters: packets sent, packets received, packets dropped, bytes sent,
  bytes received.

Avoid per-packet logs by default.

## Build And Kconfig Plan

Master environment:

- Set `ENABLE_WIFI=ON`.
- Keep `ENABLE_HTTPD=OFF` for Phase 1.
- Ensure CMake includes `esp_wifi`, `esp_phy`, `esp_netif`,
  `esp_netif_stack`, `lwip`, and `esp_coex` through the existing `ENABLE_WIFI`
  branch.
- Enable the required WiFi/lwIP sdkconfig options for master.

Other environments:

- Keep WiFi disabled for `media`, `gpu0`, `gpu1`, and `io`.
- If shared headers are touched, verify all active environments still compile.

Watch for:

- flash/RAM growth from WiFi/lwIP
- PSRAM setting interactions on master
- startup ordering with `CoreSetup`
- event-loop creation being called exactly once

## Integration Plan

The first integration target is `src/master/main.cpp`.

Current order:

1. Core setup.
2. Clock service binding.
3. WiFi setup.
4. WiFi command and network diagnostic command registration.
5. Existing RS485/SPI setup.
6. PubSub network setup.

WiFi starts before the existing wire setup so station DHCP gets a quieter boot
window and networking is available before later UDP PubSub work. Do not reorder
existing wire setup further without a concrete reason.

`CoreSetup` already initializes logging, metrics, commands, and filesystem.
That makes it the safer point after which to start WiFi, because event logs and
future credentials/errors have somewhere to go.

## Diagnostics

Phase 1 should include at least one minimal diagnostic path:

- `/wifi` prints WiFi status.
- `/udp-send <host> <port> <payload>` sends one UDP packet.
- `/udp-recv <port> [timeoutMs]` receives one UDP packet.
- `/udp-exchange <host> <port> <payload> [timeoutMs] [bindPort]` sends one UDP
  packet and receives one response on the same socket.
- `/tcp-connect <host> <port> <payload> [timeoutMs]` connects, sends, and
  receives one TCP diagnostic payload.
- `/tcp-listen <port> [timeoutMs]` accepts one TCP client and echoes one
  payload.

Prefer command-based diagnostics over synthetic always-on network traffic.

Host-side diagnostics use `bin/net-probe`, which has matching UDP/TCP one-shot
send/receive commands plus `route-get` for checking whether the local machine
can actually reach the MCU subnet directly. UDP send supports repeat counts for
short receive windows, and UDP receive can echo packets back to the peer.

## Verification Plan

Minimum verification:

1. `bin/build -e master`
2. If shared headers/config are touched: `bin/build -e media`, `bin/build -e
   gpu0`, `bin/build -e gpu1`, and `bin/build -e io`
3. Inspect size output for WiFi/lwIP flash/RAM growth.
4. On hardware, monitor master boot and confirm:
   - no startup abort
   - WiFi mode starts
   - AP or STA state transitions are logged
   - existing SPI/RS485/PubSub setup still reaches target-ready state

Optional hardware checks:

- Connect a phone/laptop to the AP.
- Confirm STA obtains an IPv4 address on the configured network.
- Send/receive a UDP diagnostic packet.
- Run `/monitor` before and after WiFi start to inspect task and heap impact.

Current validation notes:

- `bin/build -e master` succeeds with WiFi/lwIP enabled.
- The master station build connects to the configured guest network and has
  obtained `192.168.179.5` during hardware validation.
- AP mode was validated by temporarily selecting `AccessPoint` in the ignored
  local credentials header; `/wifi` reported AP started with no connected
  stations.
- After reconnecting the host to the same guest WiFi, route lookup reached the
  MCU directly on `en0`; the host had `192.168.179.6/24`.
- TCP diagnostics validate both directions: MCU client to host listener and host
  client to MCU listener both exchange and echo payloads.
- UDP MCU-to-host send validates.
- UDP exchange validates both unbound and bound MCU sockets when the MCU sends
  first and receives the host echo on the same socket.
- Unsolicited host-to-MCU UDP still times out on the current guest WiFi even
  after the MCU listener logs that its socket is bound. This looks like a
  network policy/statefulness issue rather than a basic ESP receive-wrapper
  failure, because host echoes to MCU-originated UDP flows do arrive.

## Implementation Milestones

Milestone 1: Build switches only

- Enable master-only WiFi component selection.
- Adjust sdkconfig for master.
- Build master and verify no accidental HTTP/WebSocket enablement.
- Status: complete.

Milestone 2: WiFi lifecycle wrapper

- Add config/types/facade.
- Implement ESP32 platform backend.
- Integrate AP-only or STA-only startup on master.
- Add logs and minimal metrics.
- Status: complete.

Milestone 3: STA/AP mode completion

- Add whichever mode was not implemented first.
- Add status command.
- Status: complete for mutually exclusive AP/STA. AP+STA remains out of scope.

Milestone 4: UDP/TCP helpers

- Add endpoint and packet types.
- Add UDP helper.
- Add minimal TCP client helper if still useful for Phase 1.
- Add diagnostic command.
- Status: complete for synchronous one-shot diagnostics, including UDP
  exchange diagnostics.

Milestone 5: Cleanup and documentation

- Update this document with implementation findings.
- Update `docs/networking-pubsub-web-plan.md` only if the rough direction
  changes.
- Update `docs/overview.md` if WiFi becomes part of the active master runtime
  shape.
- Status: complete.

## Pause Points

Pause before proceeding if any of these become necessary:

- Adding a new dependency.
- Adding a persistent PlatformIO environment.
- Parsing `docs/wifi.md` at build time.
- Enabling WiFi on non-master environments.
- Moving WiFi setup into `CoreSetup`.
- Reworking shared platform abstractions outside the WiFi/Network path.
- Adding AP+STA behavior without a concrete channel/reconnect design.
- Adding background socket tasks without a concrete owner and bounded queues.
- Changing PubSub transport semantics as part of Phase 1.

## Open Decisions

- Whether and when to move credentials from ignored local headers to NVS.
- Whether WiFi status should be a global service or stay as a master-local
  object until Phase 2 needs it.
- Whether the diagnostic TCP receive requirement should be loosened for
  one-way connectivity checks.
- Whether to tune station reconnect/backoff or transmit power after observing
  the guest-network RSSI over longer runs.
- Whether Phase 2 UDP PubSub should include a startup heartbeat from the MCU to
  the configured peer so networks with stateful UDP station policy can pass
  return traffic.
