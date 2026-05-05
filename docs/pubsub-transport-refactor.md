# PubSub Transport Refactor

This document records the current understanding of the PubSub transport
refactor, the abstract transport semantics the library needs to support, and a
concrete implementation plan for reshaping the current architecture.

The goal is to make the PubSub core compatible with:

- point-to-point transports
- shared-medium master-polled transports
- future routed or broadcast-capable transports

This document intentionally describes transport semantics at the library level.
It does not assume a single fixed physical deployment, and it does not specify
wire-protocol implementation details beyond the behavior that the PubSub layer
must accommodate.

## Scope

This refactor is about PubSub infrastructure, not physical transport drivers.

In scope:

- PubSub routing semantics
- transport ownership and message lifetime semantics
- subscription propagation semantics
- egress buffering responsibilities
- local test transports that emulate transport behavior on-device

Out of scope:

- SPI, I2C, RS485, or Ethernet driver implementation
- wire timings, framing, interrupts, DMA setup, or hardware concerns
- host-side test harnesses

Backwards compatibility is not a goal. Existing APIs may change where the
current model prevents a clean transport abstraction.

## Problem Statement

The current PubSub implementation can route between multiple registered
transports, but it models transport interest and in-flight delivery at the
transport-instance level.

That is sufficient when one transport instance corresponds to one remote
routing domain, such as:

- a point-to-point local test link
- a point-to-point RS485 connection
- a future Ethernet-like transport that can naturally fan out at the transport
  layer

It is not sufficient when one transport instance fronts multiple remote peers on
a shared medium, such as:

- SPI star topologies where the master polls multiple slaves
- I2C topologies where a master addresses multiple slaves

In those cases, the current model cannot express:

- which remote peer on a transport subscribed to a topic
- which remote peer on a transport produced an ingress message
- which subset of peers behind one transport still need a message inside the
  transport's own retained fanout state
- which nodes are allowed to redistribute on that transport

## Current Behavior Summary

The current core behavior can be summarized as follows:

- `Node` receives from every transport, drains local and transport ingress, then
  sends on every transport.
- `SubscriptionManager` publishes control-plane subscribe and unsubscribe
  messages on topic `PubSub`.
- inbound control-plane messages update transport topic masks
- `Drainer` stores in-flight frames with a pending transport mask
- `Publisher` enqueues a frame to every interested transport except the ingress
  transport
- `BaseTransport` serializes an envelope, hands the bytes to the transport
  callback, and then acknowledges the original PubSub envelope

The current design already has one important and correct idea:

- the original PubSub message lifetime can end before physical transmission
  finishes, as long as the transport has taken durable ownership of the
  serialized frame

This is what transport-owned egress buffers are for.

## Ownership And Ack Semantics

The library must preserve the following ownership rule:

- the PubSub core owns an `Envelope`
- a transport may serialize that `Envelope` into transport-owned storage
- once the transport has durably taken ownership of the serialized frame, the
  PubSub core may release the original `Envelope`
- any later retention or release is then handled by the transport's own egress
  state

This means the current high-level ownership pattern is valid:

1. PubSub hands a message to a transport
2. the transport stores or otherwise takes durable ownership of the serialized
   frame
3. PubSub acknowledges the original message
4. the transport later releases its own retained frame when the protocol allows

For example:

- point-to-point RS485 may not need an egress buffer at all if handing the
  frame to the driver is already sufficient ownership transfer
- an SPI slave may need a DMA-capable egress buffer and may retain the frame
  until the master polls it
- an I2C slave may retain the frame until it is addressed and actively
  transmits it

The ack that releases the original PubSub envelope is therefore not a claim of
end-to-end delivery. It is only a claim that the transport has taken
responsibility for that message.

This distinction must remain explicit in the refactored model.

## Buffer Pressure Policy

The PubSub transport layer currently needs only two traffic classes:

- `Critical`
- `Noncritical`

`PubSub` control-plane traffic is treated as `Critical` by default. Other
application traffic is treated as `Noncritical` unless explicitly marked
otherwise when the envelope is created.

Buffer pressure is handled at the ingress and egress arena layer:

- if a `Noncritical` PDU arrives and the arena cannot retain it, the PDU may be
  dropped
- if a `Critical` PDU arrives and the arena is full, the arena may evict the
  oldest retained `Noncritical` PDU to make space
- if only `Critical` PDUs remain, the new `Critical` PDU is rejected instead of
  silently dropping retained critical traffic

These pressure events are counted through the Core-level `psCore` PubSub metric
group so the local harness can distinguish transport loss from intentional
noncritical shedding without enabling profiling. High-volume PubSub-over-SPI
tx/rx/ack counters remain in the Profiling-level `psSpi` group.

## Abstract Transport Categories

The PubSub core needs to reason about transport behavior, not protocol names.

The following transport categories are sufficient for the current target model.

### PointToPoint

A `PointToPoint` transport instance connects exactly one remote routing domain.

Characteristics:

- one transport instance corresponds to one remote subscription state
- an ingress message from that transport should not be sent back to the same
  transport
- the current transport-level routing model is close to sufficient

Examples:

- local point-to-point test transport
- project-scoped RS485 usage

### SharedBusEdge

A `SharedBusEdge` transport instance is attached to a shared medium, but this
node is not the bus router for that transport.

Characteristics:

- the transport may send frames toward the bus router
- the transport may receive frames from the bus router
- messages received from this transport may be forwarded to other transports on
  the node
- messages received from this transport must not be re-fanned out onto the same
  transport

This avoids slave-side rebroadcast loops on a shared bus while still allowing a
node to bridge between transports.

Examples:

- SPI slave transport
- I2C slave transport

### SharedBusRouter

A `SharedBusRouter` transport instance fronts multiple remote peers and is
responsible for routing between them.

Characteristics:

- the transport learns subscription state per remote peer
- the transport may receive a message from peer `A`
- the transport may then redistribute that message to peers `B`, `C`, and so on
  on later turns
- the ingress peer must be excluded from same-transport redistribution
- pending peer delivery may be tracked inside the transport after the PubSub
  core has handed off ownership

Examples:

- SPI master transport
- I2C master transport

For the local on-device test transport that emulates this role, the router does
not need to queue peer ingress twice. A clean model is:

- edge transports hand outbound frames off into a per-peer shared-bus link
  mailbox instead of exposing their private egress storage to the router task
- router ingress is materialized only when PubSub polls the transport
- router fanout may still retain transport-owned egress until peers are served

This keeps ownership explicit while avoiding an extra local queue stage that
does not exist in the eventual multi-MCU deployment, and it avoids artificial
cross-task locking on edge-private transport buffers inside the one-MCU test
harness.

### Future BroadcastCapable

The refactor should not preclude transports that can naturally fan out to many
peers without master-managed polling.

This category is not required to implement the current work, but the model
should leave room for it without forcing shared-bus semantics onto unrelated
transports.

Examples:

- Ethernet-like transport

## Routing Invariants

The refactored PubSub core should enforce the following invariants.

### Ingress Identity

Every message received from a transport must carry enough metadata to identify
its ingress context.

At minimum, the model needs:

- transport identity
- optional remote peer identity on that transport

This ingress context is required for:

- loop prevention
- peer-specific subscription tracking
- same-transport redistribution decisions

### Subscription Ownership

Topic interest behind a shared bus must be tracked per remote peer, not only per
transport.

This prevents incorrect behavior such as:

- peer `A` subscribes to topic `T`
- peer `B` subscribes to topic `T`
- peer `B` unsubscribes
- the entire transport incorrectly loses topic `T`

Point-to-point transports can still use a simpler one-peer model internally.

### Cross-Transport Forwarding

Messages received from any transport may be forwarded to other transports as
long as those transports are interested in the topic.

This is required for bridging behavior such as:

- RS485 -> SPI
- SPI -> RS485
- I2C -> Ethernet

This forwarding rule applies regardless of whether the ingress transport is
point-to-point or shared-medium.

### Same-Transport Forwarding

Same-transport forwarding must be controlled by transport category.

- `PointToPoint`: suppress same-transport re-send to the ingress transport
- `SharedBusEdge`: suppress all same-transport redistribution
- `SharedBusRouter`: allow same-transport redistribution to selected peers other
  than the ingress peer

The library must not infer this behavior from the node as a whole. It is a
property of each transport instance.

### Router-Owned Peer Fanout

On shared buses, only the router transport instance may perform same-bus peer
redistribution.

Edge-side transports must never attempt to infer which other peers on that bus
still need a message.

This keeps bus routing state centralized at the router transport and prevents
duplicate fanout or bus-local routing ambiguity.

The router transport may therefore retain its own per-peer delivery state after
the PubSub core has already acknowledged local ownership handoff. The core only
needs to know that the router transport itself accepted the frame.

## Shared-Bus Message Flow

The shared-bus flow should behave as follows.

### Edge To Router

When an edge transport produces a message:

1. the edge transport takes durable ownership of the serialized frame
2. the PubSub core releases the original envelope
3. the router later polls or reads the retained frame
4. the router injects the message into its local PubSub core with ingress
   metadata identifying the source peer

### Router To Other Peers

When a router transport receives a message from peer `A`:

1. the router publishes it locally
2. the router forwards it to any other non-ingress transports that are
   interested
3. if the same router transport has other interested peers on the same bus, it
   schedules redistribution for those peers except `A`
4. the router retains whatever transport-owned state it needs until those peer
   deliveries are complete
5. only when the router has completed the intended redistribution set may it
   release its own retained ingress copy for that message

### Edge Reception From Router

When an edge transport receives a message from the router:

1. the message is published locally on that node
2. the message may be forwarded to other transports on that node
3. the message must not be re-fanned out to the same shared bus transport

This is what prevents edge-side rebroadcast loops while still allowing
multi-transport bridging.

## Planned Architectural Changes

The refactor should preserve existing patterns where they still fit, but the
transport routing model needs to become peer-aware and transport-policy-aware.

### 1. Introduce Explicit Transport Forwarding Policy

Add an explicit transport-level forwarding policy concept.

The policy should represent the abstract routing behavior of a transport
instance, not its protocol name.

Initial required policy values:

- `PointToPoint`
- `SharedBusEdge`
- `SharedBusRouter`

This policy should be part of transport registration data so the core can make
forwarding decisions without special-casing protocol names.

### 2. Introduce Peer-Aware Ingress Metadata

Replace the current notion of "ingress transport only" with an ingress context
that can carry:

- transport identity
- optional peer identity

Point-to-point transports may use a trivial peer identity or omit it internally
if one transport always maps to one peer.

### 3. Split Transport-Level And Peer-Level Interest Tracking

The current transport topic mask model should be replaced or extended so that:

- `PointToPoint` transports can still use transport-level topic interest
- `SharedBusRouter` transports can maintain peer-level topic interest behind a
  single transport instance

This likely means transport directory structures remain useful, but same-bus
peer subscription state cannot live only in a single transport-wide topic mask.

### 4. Keep Core Pending Tracking At Transport Granularity

The current in-flight delivery tracking is transport-bitmask-based.

That can remain intact if router transports do not acknowledge the PubSub
envelope until they have taken durable ownership of:

- the serialized frame bytes
- the router-local peer target mask for later redistribution

Under that contract:

- the core tracks only whether a transport accepted the frame
- the router transport tracks which same-bus peers still need it
- the router transport releases its own retained fanout state after the last
  target peer is serviced

### 5. Keep BaseTransport Ownership Model

The current `BaseTransport` ownership handoff should be retained in principle.

The core contract should be documented clearly:

- transport `send` callback must not return success until it has taken durable
  ownership of the serialized frame
- the original PubSub envelope may then be released immediately
- the transport is responsible for later releasing its own egress storage

This preserves the usefulness of transport-owned egress buffers and avoids
holding PubSub message storage for protocol-specific transmission lifetimes.

### 6. Keep Cross-Transport Forwarding As A First-Class Behavior

The refactor must preserve and clarify the rule that a node may bridge between
unrelated transports.

This should not be treated as exceptional behavior. It is one of the main
reasons transport role must be attached to the transport instance, not to the
node.

### 7. Keep Control Plane Topic-Based, But Change Ownership Granularity

The `PubSub` control topic remains a reasonable mechanism for propagating
subscription changes.

What must change is how received control-plane events are recorded:

- point-to-point transports can still record topic interest directly on the
  transport
- shared-bus router transports must record topic interest against the ingress
  peer on that transport

## Implementation Plan

The implementation should proceed in small, reviewable phases.

### Phase 1: Document And Name The New Concepts

Introduce the minimal new vocabulary in code comments and docstrings before
major structural edits.

Targets:

- transport forwarding policy
- ingress context
- peer identity
- router transport fanout state

Outcome:

- clearer code review and safer incremental edits

### Phase 2: Refactor Routing Decisions Around Ingress Context

Change the routing path so messages are handled with explicit ingress context
instead of only an optional ingress transport ID.

Targets:

- drainer publish path
- publisher forwarding filters
- transport polling callbacks

Outcome:

- loop prevention becomes precise enough for shared buses

### Phase 3: Introduce Transport Policy Into Registration And Dispatch

Extend transport registration so each transport instance exposes its forwarding
policy.

Targets:

- transport contract or registration data
- transport directory entries
- forwarding decision logic

Outcome:

- same-transport forwarding rules become explicit and testable

### Phase 4: Replace Transport-Wide Shared-Bus Subscription Tracking

Refactor control-plane handling so shared-bus routers maintain subscription
interest per peer.

Targets:

- subscription manager
- transport directory or router-owned peer-interest state

Outcome:

- subscription updates from one peer no longer corrupt interest for other peers
  on the same bus

### Phase 5: Keep In-Flight Core Tracking Simple And Move Peer Delivery Into Router Transports

Keep the core transport-mask acknowledgement model, but tighten the ownership
contract so router transports only ack after they have taken durable ownership
of both the serialized frame and the per-peer target mask.

Targets:

- drainer dispatch selection
- router transport handoff interfaces
- router transport-owned pending peer state

Outcome:

- the PubSub core stays simple and deterministic
- one router transport can complete multi-peer fanout without expanding the
  core in-flight bookkeeping model

### Phase 6: Implement Local Test Transport Family

Add local test transports that emulate the abstract transport categories without
requiring real hardware wiring.

Targets:

- point-to-point local transport
- buffered DMA-like local shared-bus edge transport
- buffered active-send local shared-bus edge transport
- local shared-bus router transport

Outcome:

- the full PubSub routing model can be exercised on-device using only software

### Phase 7: Convert Existing Demo Or Test Wiring To New Model

Update the current on-device test setup so it exercises:

- point-to-point forwarding
- shared-bus edge-to-router ingress
- router redistribution to multiple peers
- cross-transport bridging between unrelated media

Outcome:

- the new model is validated in realistic topology combinations without
  physical transport wiring

## Planned Local Test Transports

The local transport family should emulate abstract transport behavior, not exact
driver internals.

### LocalPointToPointTransport

Purpose:

- emulate a direct point-to-point link
- serve as the simplest transport baseline
- approximate project-scoped RS485 usage

Expected behavior:

- one link endpoint per transport instance
- no retained egress storage required
- same-transport re-send suppression only

### LocalDMABufferedTransport

Purpose:

- emulate a shared-bus edge transport that must retain data in transport-owned
  memory before a master polls it
- approximate SPI slave semantics at the PubSub layer

Expected behavior:

- transport serializes and stores the full frame in an egress buffer
- `send` completes only once the frame is durably stored
- the retained frame remains available for later master retrieval
- receiving from the router must not trigger same-transport redistribution back
  onto that bus

### LocalActiveBufferedTransport

Purpose:

- emulate a shared-bus edge transport that retains a frame until the master
  addresses it, then actively transmits it
- approximate I2C slave semantics at the PubSub layer

Expected behavior:

- transport retains transport-owned egress frame state
- a later explicit read or poll operation causes the buffered frame to be
  emitted
- reception from the same shared bus remains edge-only and non-routing

### LocalSharedBusRouterTransport

Purpose:

- emulate a master-polled shared-bus router transport
- exercise peer-aware subscription tracking and redistribution logic on-device

Expected behavior:

- multiple local peer transports may attach behind one router transport
- the router polls peers one at a time
- ingress from peer `A` may later be redistributed to peers `B`, `C`, and so on
- same-bus redistribution excludes the ingress peer
- router delivery state is tracked per target peer inside the router transport

This transport is necessary to test the core shared-bus model. Edge transports
alone are not sufficient.

## Suggested Code Touch Points

The following areas are expected to require the main architectural changes:

- `include/PubSubBackend/detail/Types.hpp`
- `include/PubSubBackend/detail/Drainer.hpp`
- `include/PubSubBackend/detail/Publisher.hpp`
- `include/PubSubBackend/detail/SubscriptionManager.hpp`
- `include/PubSubBackend/detail/ITransport.hpp`
- `include/PubSubBackend/detail/TransporterDirectory.hpp`
- `include/PubSubBackend/Transports/BaseTransport.hpp`
- `include/PubSubBackend/Transports/LocalTransport.hpp`
- `include/PubSubBackend/Transports/LocalBufferedTransport.hpp`
- `src/master/main.cpp`

The following areas are expected to remain conceptually stable:

- envelope structure and wire serialization
- subscriber callback model
- control-plane use of a dedicated `PubSub` topic

## Open Questions To Resolve During Implementation

These questions do not block the plan, but they should be answered explicitly as
the code is changed.

- What is the smallest bounded peer-identity type that works across transports?
- Should router-side peer subscription state live in the generic transport
  directory or in router-specific transport state?
- Is the current transport-plus-peer split sufficient for future routed or
  broadcast-capable transports, or would a broader target descriptor become
  necessary later?
- Which transport contract methods should remain generic, and which
  shared-bus-specific behaviors should be isolated inside router transports?
- How should fairness be enforced when a router transport has many peers with
  pending ingress and egress work?
- What bounded backpressure policy should apply when router-side per-peer
  buffers fill up?

## Non-Goals For The Refactor

The refactor should avoid the following unless implementation proves them
necessary.

- changing public APIs for style-only reasons
- embedding protocol names into generic routing logic
- turning node roles into global "master" or "slave" identities
- forcing all transports to behave like shared-bus transports
- introducing unbounded dynamic memory or non-deterministic ownership schemes

## Expected End State

After the refactor:

- point-to-point transports should remain simple
- shared-bus edge transports should be able to retain frames and bridge to other
  transports without same-bus rebroadcast
- shared-bus router transports should be able to poll peers, learn peer-level
  subscriptions, and redistribute selectively on the same bus
- local software transports should be able to exercise these behaviors entirely
  on-device without requiring physical hardware setups

That end state is sufficient to begin real transport implementation work later
without forcing the PubSub core to be redesigned again for SPI or I2C.
