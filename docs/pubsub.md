# PubSub Architecture

This document is the current handoff reference for the PubSub subsystem. It
describes the active architecture, the local performance harness, and the rules
that should guide future changes.

## System Role

PubSub is the coordination layer for a multi-MCU LED system. It is designed for
static storage, bounded queues, deterministic ownership, and low overhead on
ESP32-class MCUs.

The target topology is:

- one master node that coordinates the bus and handles WiFi
- one media node that publishes FFT and beat events from I2S audio
- four GPU nodes that render LED segments locally
- side nodes over RS485, I2C, and BLE for sensors, bulbs, and peripherals

The intended data model is event-driven. High-rate LED frame data should not be
broadcast through PubSub. Nodes publish compact events and state updates; GPU
nodes subscribe to those topics and generate their own frame output.

## Traffic Classes

PubSub uses two traffic classes:

- `Critical`: control-plane traffic, currently the `PubSub` topic
- `Noncritical`: normal application events such as FFT, beat, sensor, logs, and
  metrics unless explicitly marked otherwise

Under arena pressure, noncritical frames may be dropped or evicted. Critical
frames may evict older noncritical frames but should not silently displace
critical traffic.

## Core Flow

Each `PubSubBackend::detail::Node` owns:

- a local publish queue
- a transport directory
- a subscriber directory
- a subscription manager
- an ingress buffer
- a drainer that publishes local and transport ingress
- its own message ID counter

A task step receives from transports, replays due subscriptions, drains queued
messages, then lets transports send pending egress. Transport and subscriber
fanout is based on topic masks maintained by subscription control messages.
Subscription replay is normally triggered by transport availability. A config
may also enable periodic replay for hardware bring-up or transports where a
missed availability-era control frame should be repaired as soft state rather
than leaving later application publishes unrouted.

Payload pools are storage helpers, not PubSub node owners. Application pools may
use the facade-level no-argument message ID callback, while node-owned
control-plane publishers pass explicit IDs from their owning `Node` so the
single-device simulator can keep multiple nodes independent.

Transport ingress may keep the serialized wire image in the ingress buffer.
Forwarding paths can reuse those bytes without decoding and reserializing. Full
CRC validation is deferred until local payload access, so the active invariant
is: payload access implies frame validation.

When an ingress frame has no local subscriber interest and is not control-plane
traffic, the node may cut through by inspecting only the header and forwarding
the original serialized frame directly to interested egress transports. Hardware
transports own a short wire in-flight copy for raw cut-through sends, so this
path avoids PubSub ingress storage and drainer in-flight retention.

## Transport Model

The core distinguishes three transport forwarding policies:

- `PointToPoint`: one transport instance represents one remote routing domain
- `SharedBusEdge`: a non-router peer on a shared bus, such as an SPI slave
- `SharedBusRouter`: a router for multiple shared-bus peers, such as an SPI
  master

Shared-bus routers track peer interest separately. A frame received from peer A
on a shared bus may be redistributed to peers B/C/etc. on the same transport,
but must not be reflected back to A.

`PubSubBackend/Transports/Rs485Transport.hpp` is the hardware point-to-point
RS485 transport. It serializes PubSub frames into RS485 `Data` payloads using
`PayloadType::PubSub`, retains each egress frame in a bounded in-flight slot,
and releases the PubSub drainer ack only when the RS485 write completion
callback fires. Ingress frames are copied into a small RX queue from the RS485
data handler and published during the normal PubSub transport polling path.

`PubSubBackend/Transports/SpiTransport.hpp` is the hardware point-to-point SPI
transport used by edge nodes and by the master's low-speed media link.
`PubSubBackend/Transports/SpiRouterTransport.hpp` fronts multiple hardware SPI
links as one shared-bus router transport. The current master star setup keeps
transport IDs named for physical buses: low-speed SPI is a media point-to-point
link, while high-speed SPI is one router transport with GPU0 and GPU1 peers.
Do not model the target topology as one master PubSub transport per endpoint:
the high-speed bus must eventually host the four GPU nodes, and the low-speed
bus must host media plus future LoRA and GPS peripherals.

Application PubSub envelopes require a synced clock before creation. The wire
header carries both the legacy millisecond timestamp and a synced microsecond
timestamp so peer-side consumers can compute end-to-end latency from the
received envelope. PubSub control-plane subscription events may still be
created before clock sync so link discovery can complete; those frames carry a
zero microsecond timestamp when no synced clock is available.

## Harnesses

`include/Setups/PubSubTest.hpp` provides a single-device local shared-bus
simulator. Earlier SPI topology experiments instantiated:

- master with one shared-bus SPI router
- A1, A2, A3, A4, and bridge C as SPI shared-bus edges
- bridge C connected to node D through a point-to-point local RS485 link

The local shared-bus simulator is intentionally not a final SPI driver. Its job
is to validate PubSub routing, fanout, ownership, and rough throughput before
hardware is wired.

In the simulator, local link queues are the durable handoff point. Shared-bus
edge and router transports should not retain an additional egress arena copy if
they immediately release it after queueing bytes into a link. That pattern adds
locks, scans, copies, and static storage without representing the final wire
driver.

The active hardware harness for the current multi-board stage is
`include/Setups/PubSubStarTest.hpp`. It runs one PubSub node per MCU:

- master registers low-speed SPI, high-speed SPI, and RS485 transports and only
  bridges traffic
- media subscribes to synthetic `Power` events and publishes synthetic `Beat`
  events
- GPU0 and GPU1 subscribe to synthetic `Power` and `Beat` events through the
  high-speed SPI router transport
- IO publishes synthetic `Power` events and subscribes to synthetic `Beat`
  events

The older `include/Setups/PubSubRs485Test.hpp` and
`include/Setups/PubSubSpiTest.hpp` harnesses remain useful for focused
point-to-point transport bring-up. Hardware harness reports split application
publish counters from transport counters so a local publish with no remote
route does not look like a successful wire transmission.
The star harness also reports receive latency per logical route, for example
`io->gpu0 power`, `io->gpu1 power`, and `media->io beat`, instead of
collapsing every received test packet into one node-wide bucket. Latency
summaries include rough fixed
histogram percentiles (`p50`, `p90`, `p99`) and a `targetMiss` count for packets
above the current 10 ms propagation target. These percentiles are intentionally
bucketed rather than exact to keep the on-device memory cost small.

Transport availability can change while the drainer is publishing a frame. A
transport enqueue failure after in-flight storage should release that target and
drop the frame for that transport instead of stopping the PubSub task; later
availability replay is responsible for control-plane recovery.

Cut-through forwarding follows the same per-target drop rule for egress
backpressure. A saturated target transport may lose that non-local data frame,
but backpressure must not force the bridge back into the buffered drainer path
or stop the PubSub runner.

Malformed transport ingress frames are recoverable. Transports should drop
frames that fail the fixed PubSub header/size check and keep the PubSub runner
alive. SPI PubSub ingress also treats RX queue backpressure as a local drop
instead of NACKing the peer, because current wire write NACKs release the
sender's PubSub frame rather than retrying it.
Duplicate ingress records are also recoverable. Subscription replay and link
recovery can legitimately deliver the same control-plane frame more than once,
and that should not stop the PubSub task.

Transport ingress records are retained while forwarding paths and transport
send queues still reference them. The ingress arena therefore must not evict
old records to admit new traffic; under pressure, transports should drop the
new ingress frame and keep the runner and wire protocol alive.

Transport write completions may arrive from a transport-owned task while the
PubSub runner is enqueueing new frames. The drainer owns the in-flight frame
table and protects that table with a short critical section; release callbacks
run after the slot has been removed from the table.

## Scheduling Rules

PubSub tasks are event-driven. They should use task notifications for early
wakeups and should not run at the same priority as the harness publisher:

- set `useNotify = true`
- set `noCatchup = true`
- keep the active PubSub harness task priority at `3`
- leave PubSub node task core affinity free in harnesses unless a measurement
  proves a better placement
- keep periodic intervals for watchdog and baseline polling only

Pinning all PubSub node tasks to one core is a known-bad experiment in this
single-device local simulator. It concentrates the simulated topology on one
CPU, backlogs the small local link queues during boot traffic, and can make
PubSub tasks fail on queue send timeouts. Keep affinity free unless a clean
measurement proves a different placement is better.

## Performance Guidance

When optimizing PubSub, measure and reason at the path level:

- ensure ESP-IDF compiler optimization matches the intended benchmark mode;
  `release` throughput tests should not run with debug optimization
- avoid extra queue stages in the local simulator unless they model a real
  physical ownership boundary
- avoid arena retention for frames whose durable owner is already a transport
  or link queue
- keep transport ingress in one pass; `pollInto()` owns the receive-and-publish
  path so node steps do not snapshot transports twice for ingress
- avoid decode / encode cycles on forward-only traffic
- avoid zero-initializing stack scratch buffers that are immediately filled
- keep direct-relay paths conservative unless routing correctness is obvious
- prefer bounded static buffers over dynamic allocation

`LoggingMinimum::pubSub` and `LoggingMinimum::rs485` control whether verbose
hot-path packet trace logs are compiled for PubSub and RS485 respectively.
Leave the static minimum above `LogLevel::Verbose` for throughput runs; the trace
points are meant to localize queueing, task wakeup, and wire transaction delays
during diagnosis.

The current hot path is expected to be dominated by task scheduling, queue
handoff, transport fanout, and payload serialization only when local delivery is
required. If cut-through or serialization changes have no effect on runtime
metrics, first inspect scheduling, notification, polling, queue, and lock
behavior.

## Test Harness Guidance

The harness should validate:

- expected recipients receive exactly one copy
- payload bytes are preserved
- original pool records are released
- transport-owned egress records are released where transports actually retain
  egress
- timeouts distinguish receipt, pool release, and egress release failures

Harness traffic should be deterministic and cheap. It should not add expensive
pseudo-random generation, logging, or unnecessary buffers to the measured hot
path unless those costs are the thing being tested.

## Development Rules

Keep PubSub changes minimal and reviewable:

- preserve static ownership and bounded storage
- do not add dependencies without approval
- do not introduce runtime polymorphism unless it removes real coupling or is
  already required by transport abstraction
- update this document when ownership, scheduling, or transport semantics
  change
- build `master`, `media`, `gpu0`, and `io` after hardware PubSub changes
