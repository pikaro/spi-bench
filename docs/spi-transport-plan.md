# SPI Wire Transport

This file describes the current SPI wire implementation and the protocol shape
it should preserve. It is not a backlog dump. Remove bring-up workarounds from
this document once they stop describing the active protocol.

## Current Status

The current implementation is a point-to-point SPI link. The active hardware
star instantiates that link once for the low-speed SPI bus and once for the
high-speed SPI bus. Each bus currently has one attached slave, so this is not
yet a shared-bus implementation:

- public facade: `include/Wire/Spi/Facade.hpp`
- configuration: `include/Wire/Spi/Interfaces/MasterConfig.hpp` and
  `include/Wire/Spi/Interfaces/SlaveConfig.hpp`
- protocol packing/parsing: `include/Wire/Spi/detail/Pdu.hpp`,
  `SlotBuffer.hpp`, and `Transceiver.hpp`
- link runners: `include/Wire/Spi/detail/Master.hpp` and `Slave.hpp`
- ESP-IDF binding: `include/Wire/Spi/detail/platform/PlatformESP32.hpp`
- PubSub bridge: `include/PubSubBackend/Transports/SpiTransport.hpp`
- focused point-to-point hardware test harness:
  `include/Setups/PubSubSpiTest.hpp`
- current multi-board PubSub star harness:
  `include/Setups/PubSubStarTest.hpp`

The active PubSub star has `env:master` linked to `env:media` on the low-speed
SPI bus and `env:gpu0` on the high-speed SPI bus. The master owns both SPI
clocks; each current slave keeps a DMA transfer queued and asserts its own
attention GPIO when the master should clock that link. `env:io` joins the same
PubSub graph through RS485.

The shared-bus SPI router and multi-slave scheduler are not implemented. Do not
describe the current SPI transport as a shared-bus design; the current
multi-node proof works by registering two bus-scoped point-to-point transports
on the master PubSub node. The target is still multi-peer per bus: four GPU
nodes on the high-speed bus, and media, LoRA radio, and GPS on the low-speed
bus.

## Target Shape

SPI is intended to be the short-distance, high-throughput alternative to the
RS485 transport. The design bias is therefore:

- bounded static storage, DMA-compatible buffers, and no hot-path allocation
- full-duplex turns that carry useful data in both directions when possible
- multiple logical frames per SPI turn
- minimal protocol chatter, especially no steady-state positive ACK frames
- attention-driven scheduling so idle slaves are not polled at high rate
- small metadata checks in SPI, with payload integrity owned by the L7 protocol

A 60-byte PubSub test payload serializes to about an 88-byte PubSub frame. A
single SPI `Data` frame adds a 19-byte slot header and a 10-byte frame header,
so one test message is roughly 117 protocol bytes before bucket padding. A
10 MHz SPI bus should not be close to saturated by tens of kB/s of payload
traffic; if it is, the cause is scheduling, acknowledgement, copy, logging, or
clocking overhead rather than raw bus bandwidth.

## Slot Protocol

Each SPI turn clocks one bucketed slot. Supported bucket sizes are:

- 64 bytes
- 256 bytes
- 512 bytes
- 1024 bytes
- 4096 bytes

`SlotHeader` contains:

- protocol preamble, version, and header size
- slot length and bucket length
- peer id and connection id
- slot sequence
- cumulative frame acknowledgement sequence
- slot flags
- frame count and payload byte count
- CRC8 over the metadata header

`FrameHeader` contains:

- frame type: `Data`, `Request`, `Response`, `Nack`, `Hello`, `Heartbeat`,
  `Status`, or `Nop`
- payload type, currently `Raw`, `PubSub`, or `Clock`
- frame sequence and optional `responseTo`
- payload length and frame flags
- CRC8 over the metadata header

The SPI layer validates slot and frame metadata. PubSub frames carry their own
CRC32. Clock payloads are small structured request/response values. Raw payload
types do not get payload integrity unless the owning L7 protocol adds it.

## Acknowledgement Semantics

Successful `Data` acknowledgement is cumulative and carried in the slot header:

- `SlotFlags::Ack` means `ackSequence` is valid.
- `ackSequence` acknowledges all pending ack-required data frames up to that
  frame sequence, using modular 16-bit ordering.
- A successful data dispatch does not append a positive `Ack` frame.
- Dispatch failure appends an explicit `Nack` frame for that failed frame.
- Receivers process explicit `Nack` frames before the cumulative header ACK, so
  a failed frame is not accidentally released by a later cumulative ACK in the
  same slot.

This avoids the old steady-state pattern where every PubSub `Data` frame caused
another positive ACK frame and often another turn. It also removes repeated
stale `ackSequence` fields from the protocol: an ACK is live only when the ACK
flag is set.

`WriteRequest` completion means the peer's wire handler accepted the data into
its ingress path. It does not mean a PubSub subscriber consumed the message, and
it is not sufficient for future shared-bus router delivery semantics.

A no-slot observation means the peer was not armed with a protocol TX/RX slot.
The sender must retain the current TX slot and must not mark header ACK state as
delivered, because the peer may not have received MOSI for that transaction.
This is especially important with the current single-queued ESP32 slave DMA
wrapper.

## Scheduling And Flow Control

Master behavior:

- runs a turn when the slave attention line is asserted, local TX exists,
  queued writes exist, an ACK/header response needs transmission, or heartbeat
  is due
- with attention configured, does not poll merely because master-originated
  writes are awaiting ACK; the slave asserts attention when ACK/data is queued
- batches queued writes into the current slot up to `maxOutboundSlotBytes`;
  writes that would exceed the current slot are deferred, and writes that cannot
  fit an empty configured window are nacked with overflow
- coalesces master-originated local writes for a small bounded window before
  clocking when the attention line is quiet; this gives the slave a chance to
  arm a matching slot and reduces extra empty turns
- backs off briefly after no-slot observations; no-slot means the slave was not
  armed, so immediate retries mostly burn CPU and can repeat the same stale
  attention edge
- uses one turn per task step for the current master/media loop; multi-turn
  bursts can over-clock stale attention while the single-queued slave is still
  completing and requeueing DMA
- tracks up to eight pending ack-required writes
- widens the receive window for attention or newly queued outbound traffic

Slave behavior:

- keeps one DMA transaction queued
- asserts attention while not ready, while local TX exists, while an exchange is
  pending/in flight, while queued writes exist, or while sent writes are waiting
  for ACK release
- processes queued PubSub writes even while a Clock exchange is in flight
- reports transfer start/completion timestamps from ESP-IDF slave callbacks

The active master/media PubSub test config uses 256-byte active windows in both
directions. This fits two current PubSub test frames per turn while avoiding
512-byte DMA windows for one-frame and ACK-heavy turns. The master's receive
window must be at least as large as the slave's largest outbound slot, and the
slave's transfer window must be at least as large as the master's largest
outbound slot. Otherwise the master can clock a short transaction while the peer
has a larger slot prepared.

## Clock Sync

Clock sync is an L7 request/response over SPI:

- the slave sends `SyncRequest{markerTimeUs}` using `ExchangeRequest`
- the transport calls `ExchangeRequest::onBeforeRequest` immediately before
  queueing/writing the request so Clock can patch `markerTimeUs`
- for SPI sync, the slave releases an already-asserted attention line if needed,
  then immediately patches `markerTimeUs`, queues the request, and reasserts
  attention after the DMA slot is armed; the frame is marked with
  `FrameFlags::AttentionSync`, so the next attention assertion is specific to
  that Clock request
- the master uses the captured attention-edge ISR timestamp only if it has a
  fresh bounded-age edge for the `AttentionSync` request; otherwise it returns
  an invalid sync response instead of falling back to a scheduler timestamp
- the master returns `SyncResponse{driftUs, flags}`, where a valid response has
  `driftUs = masterMarkerTimeUs - slaveMarkerTimeUs`
- the slave applies `driftUs` only when the response has the valid flag;
  response send time and response receive time are not part of the Clock
  calculation

The Clock wire payload is the same over SPI and RS485. SPI can provide a
hardware-correlated marker via the attention edge; RS485 currently provides the
request send-start marker. The attention marker avoids measuring scheduler
delay between "slave needs a turn" and "master eventually clocks SPI". It is
still not a laboratory-grade hardware capture: the slave timestamp is taken when
preparing the request and the master timestamp is taken in the GPIO ISR, so
request preparation and GPIO/interrupt latency remain in the sample. That error
should be microseconds-scale rather than the millisecond-scale slot scheduling
noise seen when using SPI transaction boundaries alone.

## Performance-Critical Rules

Keep these properties intact unless measurement proves a better replacement:

- SPI profiling metrics are opt-in via `include/StaticConfig/Metrics.hpp`.
  Enabling profiling counters in the hot path can materially affect CPU use.
  Metric recording is expected to stay on the backend's atomic fast path;
  avoid reintroducing per-sample `Directory` mutex lookups for SPI counters.
- Do not zero the full 4096-byte slot buffer on every slot reset.
- Do not parse a slot twice to discover whether it contains `Hello`; use the
  slot flag.
- Do not scan a full transfer window to identify no-slot observations; checking
  the preamble is enough.
- Do not block slave PubSub writes behind an unrelated Clock exchange.
- Do not clear large PubSub in-flight buffers on write completion; marking the
  slot free is sufficient.
- Keep verbose SPI and PubSub tracing compiled out during throughput tests.

The PubSub SPI harness reports application and SPI-transport stages separately.
`app: pub=` means the test publisher attempted local PubSub publication. A
successful app publish can still be released locally without touching SPI when
the peer subscription advertisement has not arrived. `spi: txQ=` means a
serialized PubSub frame was actually queued on the SPI link, `spi: txAck=`
means the peer acknowledged the SPI write, and `spi: rxQ=` means a raw PubSub
frame reached the receiving transport queue. `spi: inFlightFull=` is a
backpressure observation while the transport's small SPI write table is full.
If `pub=` is nonzero while `txQ=` is zero, the problem is PubSub
routing/subscription state rather than raw SPI throughput.

The SPI harness enables a low-rate periodic PubSub subscription replay. This is
control-plane soft state, not benchmark traffic. It prevents one missed
subscription advertisement during link bring-up from permanently black-holing
application data until reboot.

The hardware PubSub star harness currently drives synthetic traffic from IO to
the SPI peers and from media back to IO. The larger harness message pools are
intentional: the test should expose transport and scheduling pressure before
failing from a small local payload pool.

## Known Limitations

- The SPI implementation is still point-to-point. The current multi-node
  topology uses separate master SPI buses with one slave on each bus. A real
  shared-bus SPI design still needs a master peer table, per-peer attention
  inputs, scheduling fairness, per-peer availability, and per-target PubSub ACK
  accounting.
- The slave queues one DMA transaction at a time. This keeps ownership simple
  but limits maximum transaction cadence and makes queue starvation visible as
  no-slot observations if the master clocks too early. Higher throughput should
  move to double-buffered or ring-buffered slave slots before increasing task
  rates aggressively.
- There is no retransmit window. A corrupted or missed slot causes sequence
  telemetry and eventually a pending write timeout/nack. For SPI this is
  acceptable only if the physical link is clean; persistent CRC or sequence
  errors should be fixed electrically or with an explicit retransmit design.
- ACKs are cumulative per point-to-point peer. They are not delivery receipts
  for a future multi-peer PubSub router.
- The master still uses a blocking SPI transfer call. That is acceptable for
  the current bus-owner task, but high-rate multi-slave scheduling may need an
  async master path.
- Attention-edge Clock sync still uses software GPIO timestamps. It avoids
  millisecond-scale SPI scheduling noise, but true sub-microsecond sync would
  need hardware capture of the marker edge on both peers.

## Next Measurements

After protocol changes, measure on hardware before adding more workarounds:

- PubSub app publish failures, pool-full counts, and SPI `txQ`/`txAck`/`rxQ`
  rates during the bidirectional 400 Hz stress harness traffic
- SPI task CPU, PubSub task CPU, and total turn rate
- Clock resync delta distribution and timeout rate
- SPI bad slot, CRC, missed sequence, stale sequence, and no-slot counters with
  SPI metrics enabled only for the profiling run

If CPU remains high after the ACK and slot cleanup, the next likely costs are
the PubSub transport's queue-by-value RX path and the single queued slave DMA
transaction cadence. Those should be addressed with measured changes, not by
adding more protocol sideband frames.
