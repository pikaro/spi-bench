# SPI Wire Transport Implementation Plan

This document is a working plan for a DMA-backed SPI transport that can carry
PubSub and Clock traffic between one bus master and multiple slaves. It is not a
final protocol specification, but it should be concrete enough to guide the
first implementation without rediscovering the same design constraints.

## Goals

- Support one SPI master owning one physical bus with the current four-node
  topology, while keeping the peer-selection design open for future larger
  buses.
- Allow multiple independent SPI buses, for example one slower bus to an ESP32
  classic Bluetooth node and one faster bus to ESP32-S3/C3 GPU/media nodes.
- Use DMA-backed, bounded, preallocated buffers for predictable CPU and memory
  behavior.
- Batch multiple logical protocol frames into one SPI turn.
- Support PubSub as a shared-bus transport:
  - master side as `SharedBusRouter`
  - slave side as `SharedBusEdge`
  - no reflection of frames back to the peer that originated them
- Support low-overhead Clock synchronization as an L7 protocol on the same wire
  layer.
- Support optional per-slave attention GPIOs so the master can skip idle slaves
  without high-rate polling.
- Include metrics and switchable hot-path tracing from the first implementation.

## Non-Goals For The First Version

- Full LED frame streaming over SPI. PubSub should remain event-driven.
- Runtime allocation in hot paths.
- Cross-platform SPI implementation beyond the existing ESP32 target.
- A generalized bus abstraction shared with RS485. Start with `Wire/Spi/` and
  extract common pieces only after stable duplication is obvious.
- Complex reliability semantics such as retransmit windows. Use bounded sequence
  checks, metadata CRC8, explicit ack/nack, and reconnect/rehandshake first.

## Proposed File Layout

- `include/Wire/Spi/detail/`
  - `Config.hpp`
  - `Pdu.hpp`
  - `Trace.hpp`
  - `Metrics.hpp`
  - `SlotBuffer.hpp`
  - `Transceiver.hpp`
  - `Master.hpp`
  - `Slave.hpp`
  - `Scheduler.hpp`
  - `AttentionLine.hpp` if the RS485 helper is not reusable
  - `PlatformSelect.hpp`
  - `Types.hpp`
  - `platform/PlatformESP32.hpp`
- `include/Wire/Spi/Facade.hpp`
  - public `Totem::Wire::Spi::Master` and `Slave`
- `include/PubSubBackend/Transports/SpiTransport.hpp`
  - shared-bus router/edge PubSub transport
- `include/Setups/PubSubSpiTest.hpp`
  - first hardware test harness

Do not add `Wire/Spi/Interfaces/` unless external code needs to name or store
SPI-specific public types independently of `Facade.hpp`. Reuse
`Wire/Interfaces/` for cross-wire request/result and payload concepts.

## Component-Owned Platform Abstraction

SPI should not start as a top-level `Platform/` abstraction. UART sits there
because multiple components use it. SPI is expected to be owned by the SPI wire
transport, so hardware-specific code should live under
`Wire/Spi/detail/platform/` until another component genuinely needs it.

### `Wire/Spi/detail/Types.hpp`

Define compact platform-agnostic data types:

- `SpiBusId`
  - logical bus selector, not raw ESP-IDF host id
- `SpiMode`
  - mode 0 initially, leave room for mode 1/2/3
- `SpiBitOrder`
  - probably MSB first only for now
- `SpiDeviceConfig`
  - clock rate
  - mode
  - MOSI/MISO/SCLK/CS pins
  - DMA max transfer bytes
  - optional attention pin
- `SpiTransfer`
  - `std::span<const std::byte> tx`
  - `std::span<std::byte> rx`
  - timeout
- `SpiEvent`
  - transfer complete
  - error
  - overflow / missed transaction for slave mode if exposed by ESP-IDF

Keep `Wire/Spi/detail/Types.hpp` independent from ESP-IDF. It may use
`::platform::Pin` like other hardware-facing component configs, but it should
not include ESP-IDF headers.

### `detail::platform::SpiMasterBus`

Responsibilities:

- Own one ESP-IDF SPI bus host.
- Initialize MOSI/MISO/SCLK once.
- Add/remove slave devices by CS pin and per-device clock rate.
- Submit DMA transfers for one slave at a time.
- Provide either a blocking `transfer()` for the first implementation or a
  queued async API if ESP-IDF callback behavior is needed immediately.
- Invoke a platform-agnostic callback or wake the owning task when an async
  transfer completes.

Recommended first shape:

```cpp
class SpiMasterBus {
  ReturnCode init(SpiMasterBusConfig config);
  std::expected<SpiDeviceHandle, ReturnCode> addDevice(SpiDeviceConfig config);
  ReturnCode transfer(SpiDeviceHandle device, SpiTransfer transfer);
  ReturnCode deinit();
};
```

Blocking transfer is acceptable initially because the master scheduler task is
the bus owner. Move to async only if profiling shows transfer blocking harms
latency elsewhere.

### `detail::platform::SpiSlaveDevice`

Responsibilities:

- Own one ESP-IDF SPI slave peripheral.
- Queue one or more DMA RX/TX buffers before the master clocks them.
- Wake the SPI slave node when a transaction completes.
- Detect queue starvation or truncated transactions if the platform exposes
  this cleanly.

Recommended first shape:

```cpp
class SpiSlaveDevice {
  ReturnCode init(SpiSlaveConfig config);
  ReturnCode queueTransfer(SpiTransfer transfer);
  std::expected<SpiTransferResult, ReturnCode> waitTransfer(uint32_t timeoutMs);
  ReturnCode deinit();
};
```

The slave should keep its next TX slot ready before asserting attention. If no
application data is pending, the TX slot still carries a valid empty/status
frame.

### DMA Buffer Rules

- Buffers must be statically owned or allocated once at begin time.
- Buffer addresses and sizes must satisfy ESP-IDF DMA alignment requirements.
- Do not place large buffers on task stacks.
- Prefer one `SlotBuffer` owner per peer so buffer lifetime is visible.
- Start with max 4096-byte slots per direction per slave.
- Use bucketed transfer lengths: `64`, `256`, `1024`, `4096` bytes.
- The scheduler chooses the smallest bucket that fits the packed payload, with a
  small status bucket for empty but necessary turns.

Memory estimate:

- 4 slaves on one bus, double-buffered RX/TX at 4 KB max:
  `4 * 2 * 4096 = 32 KB`
- 5 slaves:
  `5 * 2 * 4096 = 40 KB`

This is acceptable with the current RAM headroom, but should remain a deliberate
budget and not become an excuse for extra queues or copies.

## Wire Protocol

SPI is physically full-duplex, but the logical protocol should be treated as a
batch exchange per selected slave.

Each master turn:

1. Master selects one slave.
2. Master packs outbound frames for that slave into the TX slot.
3. Slave has already prepared its TX slot from prior queued outbound data.
4. One DMA transfer exchanges both slots.
5. Master parses the received slave slot after transfer completion.
6. Slave parses the received master slot after transfer completion.
7. Both sides publish received payloads and prepare future slots.

### Slot Header

Every DMA slot starts with a fixed header:

- preamble/version
- slot length
- bucket length
- peer id / slave id
- epoch or connection id
- sequence number
- response/ack sequence if needed
- flags
  - hello
  - heartbeat/status
  - ack/nack present
  - truncated/dropped frames
  - clock sync marker if needed
- frame count
- payload bytes used
- metadata CRC8

The header should be small enough to fit in the 64-byte status bucket with at
least one tiny control frame.

Only SPI slot and frame metadata should be protected by the SPI wire CRC, and
CRC8 is enough for that purpose. Higher-level payloads remain responsible for
their own integrity checks. This preserves PubSub's ability to peek and route a
wire header without forcing the router to compute a full payload CRC for traffic
that only an interested destination node needs to validate.

### Logical Frames Inside A Slot

After the slot header, pack a sequence of logical frames:

- frame type
  - `Data`
  - `Request`
  - `Response`
  - `Ack`
  - `Nack`
  - `Hello`
  - `Heartbeat`
  - `Status`
- payload type
  - reuse `Totem::Wire::PayloadType`, including `PubSub` and `Clock`
- logical sequence
- response-to sequence if applicable
- payload length
- payload bytes

This mirrors the RS485 conceptual split but batches frames instead of sending
one frame per wire transaction.

### Ack Semantics

For PubSub, a transport-level ack means:

- the frame was accepted into the peer's SPI slot parser and handed to the
  receiving transport ingress queue, or
- for router forwarding, accepted into the next durable routing buffer.

The PubSub drainer should not release an egress frame merely because it was
packed into a local SPI TX slot. It should release after the peer acknowledges
receipt in a later slot.

For Clock, prefer `Request` / `Response` without an intermediate transport ack.
The response may be carried in the opposite direction of the same or next slot,
depending on timing. Timestamping must record:

- local slot TX start
- local slot RX complete
- remote request receive/parse time
- remote response slot TX start

Do not reuse RS485 timestamp assumptions blindly. SPI timestamps should be
defined around DMA transaction boundaries.

### Handshake And Readiness

Each peer needs a two-way handshake:

1. Master sends `Hello` in that slave's slot.
2. Slave returns `Hello` with supported version, max slot size, bucket support,
   and feature flags.
3. Master marks peer ready and publishes transport availability.
4. Peer sequence and connection id reset on reconnect.

The attention GPIO can serve as the initial slave-to-master readiness signal.
Before protocol handshake, the line has no other useful semantic meaning: it
only tells the master that the slave wants to be clocked. Protocol readiness
still comes from `Hello`, so a noisy or stale attention assertion cannot by
itself mark the peer ready. Master readiness is implicit because the master owns
the clock and slave-select lines.

### Error Recovery

On any of these:

- invalid preamble/version
- metadata CRC failure
- impossible length
- sequence mismatch
- repeated missed heartbeat/status

the affected peer state resets to handshake mode. Only that slave should reset;
other slaves on the same bus remain ready unless the platform bus itself fails.

Recovery should:

- discard pending per-peer SPI protocol state
- nack or drop in-flight PubSub egress for that peer
- preserve unrelated peer queues
- increment metrics
- publish transport availability change to PubSub

## Master Scheduler

The master owns arbitration. Slaves never transmit unless the master clocks a
turn.

### Scheduler Inputs

- per-slave pending master-to-slave bytes
- per-slave attention GPIO level
- per-slave subscription interest / topic masks from PubSub
- per-slave readiness and error state
- periodic heartbeat/status deadlines
- clock sync deadlines
- bus cycle budget

### Scheduler Policy

Start simple:

- cycle period: 10 ms
- per bus: configurable max turns per cycle
- per turn: one slave, one bucketed DMA transaction
- ready slaves with asserted attention outrank idle slaves
- slaves with pending master egress outrank idle slaves
- heartbeat/status turns happen at low frequency
- unused budget rolls to ready peers in round-robin order
- quiet slaves can be skipped until heartbeat/status deadline

Example:

1. Refill cycle budget every 10 ms.
2. While budget remains:
   - choose next peer with urgent attention or egress
   - else choose next peer needing heartbeat/status
   - else stop; do not poll empty peers
3. Pick smallest bucket that fits current outbound batch and minimum inbound
   status requirement.
4. Perform DMA transfer.
5. Parse inbound slot.
6. Update metrics, ack state, and PubSub availability.

Avoid high-rate empty polling. Attention GPIO exists to prevent that.

### Multi-Bus Support

Represent each physical bus as one instance of the same `Spi::MasterBus` class,
with its own configuration, TaskController runner, and peer table. A slower
classic ESP32 bus and a faster S3/C3 bus should not become separate classes.

Possible topology:

- `SpiBusClassic`
  - lower clock, one ESP32 classic node
- `SpiBusFast`
  - higher clock, media/GPU nodes

PubSub sees these as transports:

- one shared-bus router per physical bus, or
- one router transport owning multiple bus instances if that proves simpler

Prefer one router transport per physical bus initially. It keeps metrics,
configuration, and failures isolated.

The current hardware bring-up should assume the available native ESP32
slave-select lines and four total nodes. The public peer-selection boundary
should still remain narrow enough that a future GPIO-driven CS, mux, or shift
register implementation can replace native SS selection without rewriting the
wire protocol or PubSub transport.

## Slave Node

The SPI slave node owns:

- platform `SpiSlaveDevice`
- one RX slot buffer
- one TX slot buffer
- outbound logical frame queue
- registered L3 handlers by `PayloadType`
- attention output
- metrics

Slave flow:

1. Prepare TX slot from queued outbound frames or status.
2. If outbound data exists, assert attention.
3. Queue DMA transfer and wait for master clock.
4. On transfer completion, release attention if no more outbound data remains.
5. Parse master slot.
6. Invoke L3 handlers and PubSub ingress.
7. Queue ack/response frames for a future TX slot.
8. Re-arm DMA transfer.

Critical point: the slave must have a valid TX buffer queued before asserting
attention, otherwise the master may clock an empty or stale slot.

## PubSub Transport

Add `PubSubBackend/Transports/SpiTransport.hpp`.

### Master-Side Router

The master transport should behave like a `SharedBusRouter`:

- maintain peer id for each SPI slave
- maintain per-peer topic interest
- accept outbound PubSub frames from the node drainer
- route frames to one or more peer egress queues
- avoid reflecting frames to their source peer
- release PubSub egress after peer ack, not after local packing
- publish transport availability per peer when handshake changes

For forwarding:

- use raw serialized PubSub frames where possible
- avoid decode/re-encode for pass-through traffic
- direct relay into per-peer SPI egress queues if the target is obvious and
  queue capacity exists
- fall back to existing buffered ingress path when routing is complex

### Slave-Side Edge

The slave transport should behave like a `SharedBusEdge`:

- one remote routing domain: the SPI master/router
- local publishes enqueue frames into the slave SPI node
- received PubSub frames are published via `BaseTransport::pollInto()` flow
- egress is released after master ack

### Queues And Backpressure

Use bounded per-peer queues:

- one queue for logical outbound frames waiting to be packed
- one small in-flight table for frames awaiting peer ack
- one RX ingress queue from SPI node to PubSub transport

Do not retain more copies than necessary:

- PubSub serialized wire image should be copied once into the SPI durable egress
  queue or referenced through an existing arena only if lifetime is guaranteed.
- Packing a frame into a DMA slot is not durable ownership.
- After a slot is acknowledged by the peer, release the corresponding PubSub
  drainer ack.

Backpressure behavior:

- critical PubSub control frames should be preserved where possible.
- noncritical traffic may drop under pressure.
- overflow counters must distinguish local PubSub allocation failure, SPI
  egress queue full, in-flight table full, and DMA slot truncation.

## Clock Sync Integration

Clock should use `PayloadType::Clock` over SPI.

Recommended first protocol:

- slave sends Clock `Request` in its next outbound slot.
- master parses request and queues Clock `Response` for the next slot to that
  slave.
- slave computes offset after receiving the response.

For time precision, add timestamp hooks around slot transfer:

- `slotTxStartUs`
- `slotRxCompleteUs`
- `requestParsedUs`
- `responseSlotTxStartUs`

For DMA transfers, `slotTxStartUs` should be captured immediately before
submitting or starting the transaction, not when the logical frame is queued.
`slotRxCompleteUs` should be captured as close as possible to transfer
completion notification.

Clock sync traffic should bypass normal low-priority batching when a sync is
pending, but it should still use the same slot protocol so scheduler behavior is
observable.

## Attention Lines

Use one active-low open-drain attention line per slave when latency matters.

Master side:

- input with pull-up
- GPIO interrupt wakes the SPI master scheduler runner
- scheduler samples line level and services asserted peers first
- before handshake, an asserted line means "clock me for protocol startup"

Slave side:

- open-drain output
- assert only after the next TX slot is prepared and DMA is queued
- keep asserted while outbound queue remains nonempty
- release after transfer completion if the queue is empty
- before handshake, assert after the initial `Hello` response/status slot is
  prepared

This mirrors the RS485 attention pattern, but per slave rather than one
point-to-point line.

## Metrics

Add `Wire/Spi/detail/Metrics.hpp` early.

Per bus metrics:

- turns per second
- bytes TX/RX per second
- bus utilization estimate
- scheduler idle cycles
- scheduler budget exhausted cycles
- average/max scheduler loop time

Per peer metrics:

- ready/not-ready state changes
- handshakes
- reconnects
- attention assertions serviced
- attention-to-transfer latency
- transfer count by bucket size
- payload bytes packed by bucket size
- dropped frames by reason
- ack latency
- PubSub frame latency min/avg/max
- Clock sync latency and offset samples
- metadata CRC failures
- sequence errors
- DMA/platform errors

Metrics should be cheap counters updated in hot paths and reported by the
existing metrics/logging infrastructure at low frequency.

## Trace Logging

Add `Wire/Spi/detail/Trace.hpp` similar to RS485:

- gated by `tracing_for(Tracing::spi)`
- inline `log_trace_slot(...)`
- inline `log_trace_frame(...)`
- compiled to no-op when disabled

Trace points:

- scheduler selected peer
- bucket selected
- slot packed
- DMA start
- DMA complete
- slot parsed
- frame acked/nacked
- peer reset
- PubSub ingress/egress handoff
- Clock request/response timestamp points

Do not scatter `#ifdef`s through protocol code. Keep flags inside trace helper
functions, following the same constexpr `Tracing` pattern as current RS485 /
PubSub tracing so the compiler can discard disabled trace paths.

## Step-By-Step Implementation Plan

### Phase 1: Platform SPI Foundation

1. Add `Wire/Spi/detail/Types.hpp`.
2. Add `Wire/Spi/detail/PlatformSelect.hpp`.
3. Implement `detail::platform::SpiMasterBus` for ESP32:
   - bus init/deinit
   - add device
   - blocking DMA transfer
   - timeout/error mapping
4. Implement `detail::platform::SpiSlaveDevice` for ESP32:
   - slave init/deinit
   - queue/wait transfer
   - completion event mapping
5. Add a minimal compile-only setup using one master and one slave config.
6. Build `master` and `slave`.

Critical checks:

- DMA buffer alignment compiles and runs on ESP32-S3 and ESP32-C3.
- SPI bus ids do not leak ESP-IDF host ids outside platform code.
- No platform-specific headers are included by `Wire/Spi`.

### Phase 2: Slot Format And Local Packing

1. Add `Wire/Spi/detail/Pdu.hpp`.
2. Define slot header, frame header, metadata CRC8 helpers, bucket enum.
3. Add `SlotBuffer`:
   - fixed max capacity
   - reset
   - append frame
   - finalize metadata CRC
   - parse iterator
4. Add host-side or compile-time tests if practical; otherwise add a small
   on-device self-check path behind a setup flag.
5. Add SPI trace helpers using `tracing_for(Tracing::spi)`.

Critical checks:

- Parser rejects impossible lengths before touching payload spans.
- metadata CRC failures reset only the affected peer.
- Empty/status slots are valid frames, not special cases.

### Phase 3: One Master And One Slave Wire Link

1. Add `Wire::Spi::Master` with one peer.
2. Add `Wire::Spi::Slave`.
3. Implement handshake.
4. Implement heartbeat/status.
5. Implement `Data`, `Request`, `Response`, `Ack`, `Nack` frame handling.
6. Add handler registration by `PayloadType`.
7. Add basic metrics.
8. Validate Clock sync over SPI with one master and one slave.

Critical checks:

- No PubSub integration yet; keep wire layer behavior isolated.
- Reset on malformed slot must recover without reboot.
- Slave always has a valid TX slot queued before attention.

### Phase 4: Attention And Event-Driven Scheduling

1. Add per-peer attention config.
2. Reuse `platform::Gpio` and ISR-safe TaskController signal path.
3. Master scheduler wakes on attention and services asserted peers.
4. Slave asserts attention when queued outbound data exists.
5. Add attention metrics.
6. Confirm latency improvement versus periodic polling.

Critical checks:

- No ad-hoc platform tasks for GPIO.
- ISR path only records state and wakes the existing runner.
- Sustained low attention causes repeated service until the peer drains.

### Phase 5: PubSub Edge Transport

1. Implement slave-side `SpiTransport` as `SharedBusEdge`.
2. Serialize PubSub frames into SPI egress queue.
3. Release egress on master ack.
4. Publish received frames through existing `BaseTransport` receive path.
5. Add counters for enqueue, ack, drop, overflow, and latency.
6. Validate one-way and bidirectional PubSub traffic with one slave.

Critical checks:

- Control-plane PubSub traffic must survive normal pressure.
- Noncritical drops must be visible in stats.
- PubSub task must wake on SPI ingress.

### Phase 6: PubSub Router Transport

1. Implement master-side `SpiTransport` as `SharedBusRouter`.
2. Track per-peer subscription interest.
3. Route local master frames to interested SPI peers.
4. Route frames received from one SPI peer to other interested peers.
5. Avoid reflection to the source peer.
6. Preserve raw serialized frames for forwarding where possible.
7. Add direct relay path only after the buffered path is correct.

Critical checks:

- Subscription replay on peer ready.
- Availability changes are per peer.
- One peer reset must not reset the whole bus transport.

### Phase 7: Multi-Slave Scheduler

1. Generalize master from one peer to a static peer table.
2. Add round-robin selection with priority for:
   - asserted attention
   - pending master egress
   - clock sync
   - heartbeat/status
3. Add per-cycle budget.
4. Add bucket selection based on packed bytes.
5. Add multiple bus instances if needed.
6. Validate four fast slaves plus one slow bus/slave topology.

Critical checks:

- Budget exhaustion is measured, not guessed.
- Slow classic ESP32 bus cannot starve fast GPU bus.
- Idle slaves do not consume high-rate empty turns.

### Phase 8: Tuning And Hardening

1. Run saturation tests:
   - FFT at 10 ms
   - beat/button burst traffic
   - PubSub control replay
   - Clock sync under load
2. Measure CPU per bus and per node.
3. Tune bucket sizes and cycle budgets.
4. Tune queue depths and in-flight table size.
5. Add recovery tests:
   - slave reboot
   - malformed slot
   - missed heartbeat
   - attention stuck low
   - queue overflow
6. Document final protocol decisions in `docs/spi-wire.md` or update this
   document once it becomes the spec.

## Open Design Questions

- Exact bucket sizes: start with `64/256/1024/4096`, adjust from metrics.
- Whether master transfers should be blocking in the scheduler task or async.
- Whether Clock sync needs a dedicated priority lane inside the slot packer.
- Whether PubSub critical frames need a separate tiny reserved region in every
  slot.
- Whether the classic ESP32 node should use a separate physical bus from the
  start or only after first throughput measurements.
- Whether native slave-select lines remain sufficient in future projects, or
  whether GPIO-driven CS, a mux, or a shift register becomes useful. The first
  implementation should use native SS lines but avoid exposing that detail above
  the platform/bus configuration boundary.

## Expected Data Flow

Master-originated PubSub event:

1. Application publishes PubSub envelope.
2. PubSub node routes to SPI router transport.
3. SPI transport queues serialized frame for each interested peer.
4. Scheduler selects peer and packs next slot.
5. DMA transfer delivers slot.
6. Slave SPI node parses PubSub frame and enqueues transport ingress.
7. Slave PubSub node wakes and delivers to local subscribers.
8. Slave queues ack in a later slot.
9. Master receives ack and releases PubSub egress.

Slave-originated PubSub event:

1. Slave application publishes envelope.
2. Slave SPI edge transport queues serialized frame.
3. Slave prepares TX slot and asserts attention.
4. Master ISR wakes scheduler.
5. Master clocks a turn for that slave.
6. Master SPI router receives frame and publishes/forwards it.
7. Router queues ack to the source slave.
8. Slave receives ack and releases egress.

Clock sync:

1. Clock client queues `PayloadType::Clock` request.
2. SPI node records request slot TX start.
3. Peer parses request and records parse/receive time.
4. Peer queues response and records response slot TX start.
5. Client receives response and records slot RX complete.
6. Clock computes offset from the four timestamps.

## Design Principles To Preserve

- Keep hardware-specific SPI code in `Wire/Spi/detail/platform` wrappers unless
  a second non-SPI component needs the same abstraction.
- Keep wire protocol independent from PubSub.
- Keep PubSub transport independent from ESP-IDF.
- Use static storage and explicit ownership.
- Prefer batched slot work over per-message SPI transactions.
- Prefer attention-driven scheduling over high-rate empty polling.
- Treat metrics as part of the feature, not a debugging afterthought.
- Keep the first implementation simple enough to reason about under logs.
