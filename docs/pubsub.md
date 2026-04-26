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

A task step receives from transports, replays due subscriptions, drains queued
messages, then lets transports send pending egress. Transport and subscriber
fanout is based on topic masks maintained by subscription control messages.

Transport ingress may keep the serialized wire image in the ingress buffer.
Forwarding paths can reuse those bytes without decoding and reserializing. Full
CRC validation is deferred until local payload access, so the active invariant
is: payload access implies frame validation.

## Transport Model

The core distinguishes three transport forwarding policies:

- `PointToPoint`: one transport instance represents one remote routing domain
- `SharedBusEdge`: a non-router peer on a shared bus, such as an SPI slave
- `SharedBusRouter`: a router for multiple shared-bus peers, such as an SPI
  master

Shared-bus routers track peer interest separately. A frame received from peer A
on a shared bus may be redistributed to peers B/C/etc. on the same transport,
but must not be reflected back to A.

## Local Harness

`src/master/main.cpp` currently instantiates the target topology on one device:

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

## Scheduling Rules

PubSub tasks are event-driven. They should use task notifications for early
wakeups and should not run at the same priority as the harness publisher:

- set `useNotify = true`
- set `noCatchup = true`
- keep the current master harness PubSub task priority at `3`
- leave PubSub node task core affinity free in the single-device harness
- keep periodic intervals for watchdog and baseline polling only

Pinning all PubSub node tasks to one core is a known-bad experiment in this
harness. It concentrates the simulated topology on one CPU, backlogs the small
local link queues during boot traffic, and can make PubSub tasks fail on queue
send timeouts. Keep affinity free unless a clean measurement proves a different
placement is better.

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
- build `master` after PubSub changes with `pio run -e master`
