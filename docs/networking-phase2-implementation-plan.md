# Phase 2 Networking Implementation Plan

This document is the working implementation plan for Phase 2 networking:
single-peer UDP PubSub transport and a first host PubSub participant.

It complements `docs/networking-pubsub-web-plan.md` and follows
`docs/networking-phase1-implementation-plan.md`. If implementation details here
contradict the rough plan, pause and decide whether the rough plan also needs to
be revised.

## Current Implementation Status

Status as of 2026-05-25:

- Firmware scaffold is implemented and build-validated for `master`, `media`,
  `gpu0`, `gpu1`, and `io`.
- Master now owns an optional UDP PubSub transport on UDP port 2026.
- The UDP transport learns the first valid peer, sends keepalives, resets stale
  peer state, and hands received PubSub frames to the existing PubSub transport
  path through a bounded queue.
- MCU-side UDP PubSub metrics are implemented in the `psUdp` group.
- A host C++ UDP peer builds locally and sends keepalives. Subscriptions are
  explicit command or local-socket requests; it does not auto-subscribe to an
  application topic.
- A Python async notify bridge wraps the C++ peer, validates PubSub envelopes
  with Pydantic, and can expose arbitrary topics through a local Unix-domain
  socket as raw newline-delimited JSON.
- The Python bridge can now ask the trusted C++ peer to publish arbitrary raw
  PubSub frames by topic and payload bytes. Generated per-type publish helpers
  are still future work.
- Live guest-network validation is still pending. The host-side guest-network
  address for the current setup is `192.168.179.6`; pass it as `--bind-ip` if
  explicit binding is needed.

Hardware status:

- Host-to-MCU UDP on port 2026 is still the first validation blocker.
- A passing build does not prove the learned-peer path until the MCU increments
  UDP RX/keepalive/control metrics from a host datagram.

## Scope

In scope:

- Master-owned UDP PubSub transport over the Phase 1 WiFi/lwIP socket layer.
- One learned UDP peer on the same routable WiFi network. The MCU listens on UDP
  port 2026 and uses the first valid sender to that port as the active peer.
- Point-to-point PubSub transport semantics for that peer.
- Bidirectional PubSub traffic. Host-to-MCU timeout is a Phase 2 blocker, not a
  known limitation to route around.
- A production `Host` PubSub node identity for host-originated frames.
- Host-side C++ PubSub peer code that sends and receives PubSub messages and
  exposes an async Python notify bridge.
- Pydantic models for Python-side message payloads where practical.
- A first Python handler template that subscribes to the low-volume ship-bell
  event and emits a log line.
- Required UDP PubSub metrics on the MCU side.
- Hardware validation with `/wifi`, `/metrics`, `bin/net-probe`, and the new host
  participant tool.

Out of scope:

- Multi-peer UDP routing.
- NAT traversal or broadcast fanout.
- Reliable UDP, retransmit queues, or remote delivery acknowledgements.
- HTTP, WebSocket, JavaScript generation, and browser participation. Those belong
  to Phase 3.
- AP+STA combined WiFi operation.
- NTP or wall-clock synchronization.
- New provisioning UX or NVS-backed credential storage.
- Enabling WiFi or UDP PubSub on non-master environments.
- New persistent PlatformIO environments.

## Assumptions

Blocking assumptions:

- Bidirectional UDP must work. If a host datagram to MCU port 2026 cannot be
  received and counted by the firmware, Phase 2 is blocked until that network or
  socket issue is fixed.
- Host is a trusted PubSub participant. The UDP path must be able to publish and
  subscribe arbitrary generated PubSub messages from the host; the first bell
  handler is only a template exercise, not a policy restriction.
- `NodeId::Host = 1U << 7` is the Phase 2 host identity. That consumes the last
  bit in the current `uint8_t` one-hot node ID space; if that is not acceptable,
  the node ID model must be widened before additional host identities land.
- UDP receive should be owned by a dedicated transport receive task with bounded
  storage. The PubSub task should consume already-received datagrams from that
  bounded handoff path, not block in `recvfrom()`.

Non-blocking assumptions:

- Host-originated envelopes use `timestampUs = 0` until a host/device monotonic
  time-sync protocol exists.
- UDP uses IPv4 only in this phase.
- Python package dependencies are acceptable when they materially improve the
  host notify bridge. Pydantic is expected for Python message models.
- Metrics names and exact group split may be adjusted to match existing metric
  naming constraints, but metrics themselves are required scope.

## Design Constraints

- Preserve bounded project-owned storage. UDP transport queues, retained raw
  frames, peer state, and host-tool buffers must all have fixed limits.
- Preserve PubSub subscription replay and backpressure behavior. Do not weaken
  replay to make UDP easier.
- Treat UDP send success as local socket acceptance only. It is not a remote
  delivery acknowledgement.
- Run UDP receive in its own named task with a fixed stack and bounded RX queue
  or slot ring. That task may block on `recvfrom()`; the PubSub task must not.
- The PubSub-facing receive callback should drain the UDP transport's bounded RX
  queue without blocking.
- Avoid per-packet logs by default. Use counters for ordinary traffic and logs
  only for state changes, invalid peer/config, and unexpected failures.
- Metrics accessors must be prewarmed before the UDP receive task starts, before
  socket callbacks can run, and before transport registration can touch them.

## Implemented Files

MCU transport:

- `include/PubSubBackend/Transports/UdpTransport.hpp`
- `include/StaticConfig/PubSubUdp.hpp` for ordinary project-owned defaults such
  as local port 2026, receive-task config, RX queue depth, keepalive interval,
  receive timeout, and peer timeout.

Metrics:

- `include/PubSubBackend/detail/Metrics.hpp` now registers UDP PubSub metrics in
  the existing PubSub metric component.

Host participant:

- `tools/pubsub-udp-peer/pubsub_wire.hpp`
- `tools/pubsub-udp-peer/pubsub_udp_peer.cpp`
- `bin/pubsub-udp-peer-cpp-build`
- `tools/pubsub_udp_peer/bridge.py`
- `bin/pubsub-udp-peer`

The host C++ codec currently mirrors the firmware wire layout in a host-safe
header. This avoids broad ESP-IDF shims for the first participant. A future
generation step should replace or feed this mirror when arbitrary generated
message coverage expands.

Integration:

- `include/Setups/PubSubNetwork.hpp`
- `include/Data/PubSub.hpp`
- `include/StaticConfig/Stacks.hpp`
- `src/master/main.cpp`
- `docs/networking-phase2-implementation-plan.md`
- `docs/networking-pubsub-web-plan.md`

## Configuration Plan

Use constexpr/static configuration for project-owned defaults:

- local UDP bind port, fixed at 2026 for this phase
- UDP receive task stack and priority
- UDP RX queue or slot-ring depth
- maximum accepted datagram bytes
- transport enabled flag
- peer-learning behavior
- keepalive interval
- peer stale timeout

Credential handling:

- Do not copy WiFi credentials into this plan or logs.
- Do not parse `docs/wifi.md` at build time.
- Do not add ignored peer config files; Phase 2 has no planned peer secrets.
- Do not introduce NVS peer storage in Phase 2. The active peer is learned at
  runtime from traffic to UDP port 2026.

## UDP Transport Shape

The UDP PubSub transport should be a `PointToPoint` transport:

- It represents exactly one remote routing domain.
- It should reuse `BaseTransport` if practical.
- It should expose `sendCallback`, `receiveCallback`, and `availableCallback`
  through a small `UdpTransportDependencies` structure.
- It should bind one UDP socket to local port 2026.
- It should learn the active peer from the first valid datagram received on that
  port.
- It should send serialized PubSub frames to the learned peer endpoint.
- It should accept ingress from only the active learned peer while that peer is
  current.
- It should drop and count datagrams from unexpected endpoints while a peer is
  active.
- It should not fragment PubSub frames across multiple UDP datagrams.
- It should own a dedicated receive task that blocks on the UDP socket and writes
  accepted datagrams into bounded transport-owned storage.
- Its `BaseTransport` receive callback should drain that bounded storage without
  performing a blocking socket receive.

`BaseTransport` already provides the main PubSub-facing contract:

- enqueue serialized frames
- send queued frames through a callback
- receive raw frames through a callback
- store or direct-dispatch ingress frames
- report transport availability changes to the PubSub node

The UDP transport should keep the hardware transport distinction clear:

- `sendTo()` success means the datagram was handed to lwIP.
- The PubSub drainer ack may be released after successful local send.
- There is no remote ack in Phase 2.
- Failed local sends increment failure metrics and allow the frame to be released
  using the same failure behavior as other transports.

Availability should be true only when:

- WiFi is enabled and started.
- Station mode has IPv4, or AP mode is active.
- The UDP socket is open.
- A peer endpoint has been learned and has not timed out.

## Peer And Flow Strategy

The MCU listens on UDP port 2026. The first valid sender to that port becomes
the active peer. "Valid" should mean at least:

- the datagram arrives on the configured socket
- the packet parses as a UDP PubSub keepalive or PubSub frame
- no peer is currently active

Once a peer is active:

- any datagram from that endpoint refreshes `lastSeen`
- datagrams from other endpoints are dropped and counted
- MCU egress goes only to that endpoint
- periodic keepalive is sent to that endpoint
- missed keepalive or stale `lastSeen` resets the UDP PubSub transport peer state

Peer reset is not an error by itself. It clears the learned endpoint, marks the
UDP transport unavailable, increments peer-reset metrics, and allows the next
valid sender to become the active peer.

The current guest-WiFi host-to-MCU UDP timeout is a blocker. Phase 2 validation
must prove that a host keepalive or PubSub frame reaches the MCU on port 2026
and causes peer learning. If this does not work, fix the network/socket behavior
before continuing with PubSub routing.

## Host Participant Shape

The host participant should be the first version of a trusted notify system:
C++ owns PubSub transport/SerDe/Codec work, and Python owns application-level
async handlers.

Required first capabilities:

- send an initial keepalive or hello to MCU UDP port 2026
- receive and decode PubSub frames from the master
- send PubSub subscription control frames to the master
- publish arbitrary generated PubSub messages from the host
- dispatch received messages to async Python handlers by generated message type
- express Python-facing decoded payloads as Pydantic models where practical
- subscribe to the ship-bell event and log one line from the async handler as
  the initial template
- print compact diagnostics and counters on exit

Host identity:

- `NodeId::Host = 1U << 7` is used for host-originated frames.
- Treat this as a single-host limitation.
- Do not use `WebSocket` transport IDs or browser-facing names for this UDP peer.

Time model:

- Host-originated envelopes should use `timestampUs = 0`.
- MCU-side latency or age calculations must skip host frames with
  `timestampUs == 0`.
- Do not add NTP in Phase 2.

Encoding direction:

- Prefer reusing the real C++ SerDe/Codec path for host encoding and decoding.
- If host compilation exposes ESP-IDF assumptions, split the smallest host-safe
  wire/codec subset rather than adding broad platform shims.
- Python should not duplicate the binary wire codec by hand. It should interact
  with C++ host PubSub code through a bridge or binding.
- Python package dependencies are acceptable when useful. Pydantic should be used
  for payload models, and a binding/helper dependency can be chosen during
  implementation if it keeps the bridge maintainable.

Current bridge shape:

- The C++ subprocess owns the UDP socket, keepalives, binary PubSub envelope
  encode/decode, CRC validation, and compact JSONL notification output. It
  emits raw `payload_hex` for arbitrary application topics.
- The Python bridge owns Pydantic validation, optional explicit per-type
  dispatch, and generic local socket fanout.
- Python sends trusted control commands to C++ over stdin:
  `publish <topic> <traffic-class> [payload-hex]`, `subscribe <topic>`,
  `unsubscribe <topic>`, `stats`, and `quit`.
- `PubSubBridge.publish_raw()` exposes arbitrary raw PubSub publishing by topic
  and payload bytes. This is the current low-level escape hatch until generated
  per-type publish helpers exist.
- The CLI has bounded `--timeout`, one-shot `--publish-raw`,
  `--subscribe-topic`, `--unsubscribe-topic`, and `--send` options, plus
  optional stdin forwarding. This lets the host tool send and receive PubSub
  traffic without attaching a serial monitor for basic validation.
- `--local-socket <path>` exposes a Unix-domain socket for local scripts.
  Clients send newline-delimited JSON `subscribe`, `unsubscribe`, and `publish`
  operations with numeric topic masks and raw payload hex. The bridge
  reference-counts local subscriptions and forwards matching raw PubSub event
  JSON to each client.

## Trusted Host PubSub Surface

The host is trusted. The UDP PubSub bridge should not impose a topic allow list
as a safety boundary.

Required direction:

- support arbitrary generated PubSub message types from the host
- keep per-type Python handlers explicit so application behavior stays readable
- let the host subscribe to any topic supported by the generated PubSub schema
- let the host publish any generated message it can construct correctly

Initial template:

- subscribe to the ship-bell button event, currently carried on the `Button`
  PubSub topic
- decode the payload into a Pydantic model
- call an async Python handler for that type
- emit a log line from the handler

That template should be easy to copy for later per-type notify handlers.

## Metrics Plan

Metrics are required for Phase 2.

Extend the PubSub metric surface with a UDP group, tentatively `psUdp`. Keep
rare failure and drop counters at `Core` or `Baseline` visibility, and put
high-volume byte counters or timing probes behind `Diagnostic` or `Profiling`
visibility if needed.

Required MCU metrics:

- UDP transport availability changes.
- UDP peer learned.
- UDP peer reset or timed out.
- UDP keepalives sent.
- UDP keepalives received.
- UDP datagrams sent.
- UDP datagrams received.
- UDP send failures.
- UDP receive failures other than normal timeout.
- UDP RX queue drops.
- UDP datagrams dropped because no peer is learned.
- UDP datagrams dropped because the peer endpoint is unexpected.
- UDP datagrams dropped because the frame is malformed, oversized, duplicate, or
  otherwise rejected by PubSub ingress.
- UDP bytes sent.
- UDP bytes received.
- Host subscription/control frames received.

Implemented MCU metrics:

- `udpAvail`: UDP transport availability changes caused by peer learn/reset.
- `udpPeer`: peer learned.
- `udpLost`: peer reset after timeout.
- `udpKTx`: keepalives sent.
- `udpKRx`: keepalives received.
- `udpTx`: PubSub datagrams sent.
- `udpRx`: PubSub datagrams accepted into the RX queue.
- `udpTxB`: UDP bytes sent, including keepalives.
- `udpRxB`: UDP bytes received, including keepalives.
- `udpFail`: socket send or receive failures other than normal receive timeout.
- `udpDrop`: oversized datagrams or RX queue drops.
- `udpBad`: malformed or corrupt PubSub datagrams.
- `udpOther`: datagrams from an unexpected peer.
- `udpNoPeer`: PubSub send attempts without a learned peer.
- `udpCtrl`: accepted PubSub control-plane frames from UDP.

Optional diagnostic/profiling metrics:

- UDP receive timeouts.
- UDP send duration.
- UDP receive task wakeups.
- UDP receive task blocked time or poll duration.
- Maximum datagram size observed.
- Current peer learned state as a gauge.
- Current RX queue depth or high-water mark.

Metric implementation constraints:

- Prewarm PubSub metrics before registering or beginning the UDP transport.
- Do not first-touch metrics from an ISR or from a socket callback that can run
  outside normal task context.
- The UDP receive task may update metrics only after the metric storage has been
  prewarmed during setup.
- Avoid per-packet logs when a metric counter is enough.
- In nominal validation, UDP failure/drop/bad-frame counters should stay at zero
  while send/receive counters move.

## Integration Plan

Master setup should remain the only production integration point.

Preferred order:

1. Core setup.
2. Clock service binding.
3. WiFi setup.
4. WiFi command and network diagnostic command registration.
5. Existing RS485/SPI setup.
6. PubSub network setup, including UDP PubSub transport when configured.
7. UDP receive task start as part of UDP transport begin, after metrics prewarm
   and socket setup.

Do not move WiFi setup into `CoreSetup` for Phase 2.

`include/Setups/PubSubNetwork.hpp` is the expected place to register the UDP
transport because it already owns the master PubSub node and transport
registration order. Keep the UDP transport optional by constexpr config so a
build can still boot with the existing SPI/RS485 network only.

## Verification Plan

Minimum build verification:

1. `bin/build -e master`
2. Because Phase 2 touches shared PubSub headers, also build:
   `bin/build -e media`, `bin/build -e gpu0`, `bin/build -e gpu1`, and
   `bin/build -e io`
3. Confirm no HTTP/WebSocket support was accidentally enabled.
4. Inspect size output for RAM/flash growth.

Minimum hardware verification:

1. Boot `master` with station-mode WiFi and UDP PubSub enabled on port 2026.
2. Confirm `/wifi` reports started station mode and an IPv4 address.
3. Confirm existing SPI/RS485/PubSub setup still reaches target-ready state.
4. Start the host participant and send the first keepalive/hello to MCU port
   2026. Example:
   `bin/pubsub-udp-peer --mcu-ip <mcu-ip> --bind-ip 192.168.179.6 --port 2026 --timeout 10s`
5. Confirm the MCU learns the host endpoint and marks the UDP transport
   available.
6. Confirm host receives MCU-originated PubSub control/replay traffic or
   keepalive response.
7. Send a host subscription control frame and confirm the MCU receives it.
8. Publish an MCU-side application event on a subscribed topic and confirm a
   local socket client receives the generic `header` plus `payload_hex` JSON.
9. Publish at least one host-originated PubSub frame and confirm the MCU receives
   and routes it as expected.
10. Let the host keepalive stop and confirm the MCU resets the learned peer
   without destabilizing the rest of PubSub.
11. Query `/metrics` before and after the exchange. Confirm UDP send/receive
   counters moved and failure/drop/bad-frame counters stayed at zero.

Guest-WiFi asymmetry checks:

- Run `bin/net-probe route-get <mcu-ip>` from the host.
- Use `bin/net-probe udp-send <mcu-ip> 2026` or the host participant keepalive
  to prove host-to-MCU UDP reaches the firmware.
- Use `/udp-exchange` or the host participant to prove MCU-to-host UDP also
  works.
- Treat host-to-MCU timeout as a blocker, because peer learning depends on it.

Regression checks:

- Existing SPI and RS485 PubSub routes should still deliver their normal traffic.
- Existing `psCore` drop/reject counters should not regress.
- Subscription replay should remain enabled and visible after UDP transport
  availability changes.

## Implementation Milestones

Milestone 1: Config, identity, and metrics

- Add UDP transport constexpr config, including port 2026, receive task, queue,
  keepalive, and stale-peer timeout settings.
- Add `NodeId::Host` for host-originated frames.
- Add required UDP PubSub metric group and counters.
- Ensure metrics prewarm still happens before transport activity.
- Status: implemented, build-validated.

Milestone 2: UDP transport and receive task

- Add `UdpTransport` around the Phase 1 UDP socket wrapper.
- Bind local port 2026.
- Add bounded receive task and queue/slot handoff into PubSub polling.
- Learn peer from the first valid sender.
- Add keepalive and stale-peer reset behavior.
- Implement `BaseTransport` callbacks.
- Register as `PointToPoint`.
- Build `master`.
- Status: implemented, build-validated; live peer learning still needs hardware
  validation.

Milestone 3: Master integration

- Register the UDP transport through `PubSubNetworkMasterSetup` when configured.
- Preserve existing SPI/RS485 transport setup and subscription replay behavior.
- Validate boot and `/metrics` on master.
- Status: implemented and build-validated; boot and `/metrics` validation are
  still pending on hardware.

Milestone 4: Host C++ peer and Python bridge

- Add host C++ peer code that can send and receive PubSub frames.
- Add Python bridge with generic notification dispatch.
- Keep message-specific Pydantic payload models or generated adapters outside
  the generic UDP participant path.
- Add host subscription control-frame send path.
- Add local Unix-domain socket fanout for arbitrary local consumers/producers.
- Validate bidirectional traffic, MCU receive metrics, and host observation.
- Status: initial implementation complete and host-build validated; live
  bidirectional traffic validation is still pending.

Milestone 5: Arbitrary host PubSub template

- Demonstrate one host-originated PubSub publish.
- Ensure the host bridge shape can construct and publish arbitrary generated
  PubSub messages, not only the first template event.
- Validate timestamp zero handling.
- Status: partial. The host is trusted, the C++ peer accepts raw publish commands
  by topic/payload, and the Python bridge exposes `publish_raw()`. Generated
  per-type publish helpers are still pending.

Milestone 6: Documentation and validation notes

- Record hardware validation findings in this document.
- Update `docs/networking-pubsub-web-plan.md` only if the rough direction
  changes.
- Update `docs/overview.md` if UDP PubSub becomes part of the active runtime
  shape.
- Status: in progress. Build validation and initial live UDP validation are
  recorded here; broader scenario validation remains pending.

## Initial Live Validation Notes

Observed on 2026-05-25 with host `192.168.179.6` and master
`192.168.179.5` on the guest network:

- `master` upload succeeded and WiFi station connected to `dre-guest`.
- Master got IPv4 address `192.168.179.5`.
- Host peer command:
  `bin/pubsub-udp-peer --mcu-ip 192.168.179.5 --bind-ip 192.168.179.6 --port 2026 --verbose`
- MCU `/metrics` while host was running showed `udpPeer=1`, `udpKRx=32`,
  `udpCtrl=1`, `udpRx=1`, and all UDP failure/drop/bad/unexpected/no-peer
  counters at zero.
- The Python bell handler logged multiple `Bell` press/release events from the
  host participant, proving MCU-to-host PubSub delivery and async dispatch.
- After stopping the host peer, MCU `/metrics` showed `udpLost=1` and
  `udpAvail=2`, proving stale-peer reset.
- Timeout-driven host command:
  `bin/pubsub-udp-peer --mcu-ip 192.168.179.5 --bind-ip 192.168.179.6 --port 2026 --timeout 3s --publish-raw 256 0 0000 --send stats --no-stdin --verbose`
  exited cleanly with final host stats:
  `keepalive_tx=3`, `keepalive_rx=2`, `frames_tx=2`, `frames_rx=30`,
  `bad_frames=0`.

## Pause Points

Pause before proceeding if any of these become necessary:

- Adding a new MCU firmware dependency.
- Adding a host dependency unrelated to the C++/Python/Pydantic bridge.
- Adding a persistent PlatformIO environment.
- Supporting more than one UDP peer.
- Adding remote ACKs, retries, or reliability semantics.
- Disabling subscription replay or weakening PubSub backpressure.
- Enabling WiFi/UDP PubSub on non-master environments.
- Adding AP+STA behavior.
- Adding HTTP, WebSocket, browser, or JavaScript model generation.
- Reworking PubSub routing semantics outside the UDP transport path.

## Open Decisions

- Exact first host-originated publish used for validation beyond raw-frame
  command coverage.
- Generated model strategy beyond the first hand-written host-safe mirror and
  Pydantic bridge models.
- Whether UDP byte counters should be `Baseline` or `Diagnostic` visibility after
  initial bring-up.

Resolved during initial implementation:

- C++/Python bridge technology is a subprocess bridge for now. C++ owns UDP and
  binary wire encode/decode, emits JSONL notifications on stdout, and accepts
  trusted publish/subscribe commands on stdin. Python owns async per-type
  handlers.
- Keepalive packet shape is a UDP-transport-local 8-byte packet:
  ASCII `TPUDPKA1`.
