# PubSub Transport Current Plan

This document is a working handoff plan for the PubSub transport work.

It is intentionally optimized for restartability after context compression. It
records:

- the current architectural state
- what has already been implemented
- what assumptions are now true
- what remains to be done
- the safest next sequence of code changes

This supersedes the older staged refactor checklist. The transport refactor has
already crossed several of those steps, and the useful information now is the
actual state of the codebase.

## User Constraints And Intent

These points came directly from the current user discussion and should be
treated as active requirements.

- the user is not trying to build a completely abstract transparent network
  layer
- the user expects to know which traffic classes run on which bus
- unrealistic traffic is considered a configuration error, not something the
  transport layer must fully save the user from
- future wire-protocol work may add bus-cost / budgeting safeguards, but that
  is a later phase
- the current optimization work must remain physically plausible for SPI and
  RS485
- pure virtual interfaces are preferred over manual vtables

Practical implication:

- optimize for the known embedded topology, not for arbitrary general-purpose
  routing

## Scope

Current target:

- keep the PubSub transport model realistic enough for planned SPI and RS485
  wire protocols
- avoid startup subscription stampedes
- reduce CPU and latency overhead in the on-device harness
- keep changes bounded and reviewable

Non-goals for the current phase:

- final wire-protocol scheduling or budgeting
- fragmentation / reassembly
- a completely abstract transparent network stack

## Current System Model

The current on-device topology in `src/master/main.cpp` is:

- one SPI shared-bus router transport on the master side
- edge transports for A1, A2, A3, A4, and bridge C
- one point-to-point local transport linking bridge C to D over simulated
  RS485

Behavioral intent:

- SPI is the high-throughput shared bus
- RS485 is the low-throughput long-distance side link
- bridge C connects the two
- transport behavior should approximate the eventual physical system, but the
  current code is still a PubSub-layer approximation, not a final wire driver

Current harness expectations:

- `publishIntervalMs = 10`
- `targetPropagationLatencyMs = 10`
- `expectationTimeoutMs = 50`
- `harnessWarmupMs = 2000`

Current startup staggering in `include/Setups/PubSubTest.hpp`:

- A1: `5ms`
- A2: `10ms`
- A3: `15ms`
- A4: `20ms`
- SPI-C: `25ms`
- RS485-C: `25ms`
- RS485-D: `30ms`

## Completed Work

### Startup / Readiness

Implemented:

- transport availability observation in `BaseTransport`
- transport-policy-aware registration through `ITransport` and
  `TransportDirectory`
- `readyAfterMs` startup delay knobs in local transports
- delayed readiness start on first availability observation rather than static
  construction time
- coalesced subscription replay in `Node`
- duplicate subscription no-op suppression in `TransportDirectory`
- peer-availability-triggered replay for shared-bus membership changes

Relevant files:

- `include/PubSubBackend/Transports/BaseTransport.hpp`
- `include/PubSubBackend/Transports/LocalTransport.hpp`
- `include/PubSubBackend/Transports/LocalSharedBusEdgeTransport.hpp`
- `include/PubSubBackend/Transports/LocalSharedBusRouterTransport.hpp`
- `include/PubSubBackend/detail/Node.hpp`
- `include/PubSubBackend/detail/TransportDirectory.hpp`
- `src/master/main.cpp`

### Routing / Correctness

Implemented:

- `TransportForwardingPolicy`
- `IngressContext`
- router peer-aware target selection
- live router peer-mask use in `Publisher::dispatchFor()`
- correct in-flight ack matching in `Drainer::ack()` using full header equality
  (`source + messageId`)

Relevant files:

- `include/PubSubBackend/detail/Types.hpp`
- `include/PubSubBackend/detail/Publisher.hpp`
- `include/PubSubBackend/detail/Drainer.hpp`

### Harness / Pressure Visibility

Implemented:

- on-device harness warm-up before publishing
- transport/pool/egress timeout tracking
- no auto-restart for PubSub harness tasks
- elevated PubSub task priority for the harness
- periodic monitoring snapshots

Relevant files:

- `src/master/main.cpp`
- `include/Setups/PubSubTest.hpp`
- `docs/overview.md`

### Performance Work Already Landed

Implemented:

- CRC path changed to use a static lookup table in `SerDe`
- shared-bus router fanout made synchronous in local simulation
- forwarding transports now reuse retained serialized ingress bytes instead of
  reserializing the same frame on every hop
- suppressed logs now short-circuit in the macros before evaluating log
  arguments
- transport ingress now does a fixed-header peek first and can defer full CRC
  validation until local payload access
- transport ingress can now bypass local ingress buffering for forward-only
  traffic when exactly one downstream transport is interested and the raw
  handoff succeeds
- local simulator link queues were reduced from full PubSub depth to a small
  fixed depth (`4`) per direction to avoid paying tens of kilobytes of static
  memory for point-to-point and router-edge links

Relevant files:

- `include/PubSubBackend/detail/SerDe.hpp`
- `include/PubSubBackend/detail/IngressBuffer.hpp`
- `include/PubSubBackend/Transports/BaseTransport.hpp`
- `include/PubSubBackend/Transports/LocalTransport.hpp`
- `include/PubSubBackend/Transports/LocalSharedBusRouterTransport.hpp`
- `include/PubSubBackend/Transports/LocalSharedBusEdgeTransport.hpp`
- `include/PubSubBackend/detail/Node.hpp`
- `include/Macros/internal/Log.hpp`
- `include/Services/Logging.hpp`

### Current Runtime Result

Latest known-good user-reported runtime state after the routing/correctness
fixes:

- correctness recovered:
  - `receiptTimeout=0`
  - `poolTimeout=0`
  - `egressTimeout=0`
  - `pending=0`
- latency still misses target:
  - end-to-end completion latency is still roughly `~30ms-50ms`
  - the user reported the average as being closer to `~40ms`
  - the system is therefore still far from the `10ms` target
- CPU is still too high:
  - roughly `86%` total on core 0
  - roughly `62%` total on core 1
  - PubSub tasks dominate usage

Interpretation:

- the main remaining problem is no longer routing correctness
- the main remaining problem is forwarding-path CPU / scheduling overhead

## Current Important Invariants

These are the assumptions the code now relies on.

### Envelope / Validation

- transport ingress frames can be retained in `IngressBuffer` as serialized wire
  images after only a fixed-header peek
- full CRC validation is no longer guaranteed at ingress-storage time
- instead, local payload access on serialized ingress records triggers
  validation before payload bytes are exposed
- forwarding can reuse the retained serialized bytes without decode/re-encode

Implication:

- the invariant is now `payload access implies validation`
- the invariant is no longer `ingress storage implies validation`

This is intentional and should be preserved unless a later design explicitly
reverts to store-and-forward everywhere

### Transport Ownership

- the PubSub core still reasons about whole frames, not fragments
- transport-owned buffering is allowed after PubSub handoff
- shared-bus router fanout may retain its own state after PubSub has released
  the original envelope
- `ByteArena` has intentionally not been redesigned and should stay that way
  unless forced by later wire-level work

### Shared-Bus Behavior

- edge transports do not rebroadcast back onto the same shared bus
- router transports may redistribute to non-ingress peers on the same bus
- router target selection is driven by per-peer interest and ingress peer
  suppression

## Current Performance Understanding

Current dominant costs before the next change:

- ingress buffering still happens for locally consumed, control-plane, and
  multi-destination transport-received frames
- direct forwarding still goes through drainer publish callbacks whenever the
  new fast path cannot be used
- the remaining major opportunity is to broaden the fast path beyond the
  current conservative single-downstream rule if measurements justify it

Important nuance:

- the current lazy-validation/raw-retain work already removed much of the
  reserialization cost
- the next likely win is eliminating unnecessary ingress retention and drainer
  work for forward-only traffic

Logging notes:

- `magic_enum::enum_name()` returns `string_view` into static storage and is
  cheap
- however, before the recent macro change, suppressed log arguments were still
  evaluated before `LoggingService::logf()` gated the call
- macro-side gating is now in place

## Active Design Direction

The next transport optimization should be:

- not full global ingress-buffer removal
- not fragmentation
- not a general transparent network layer

The current direct-relay fast path is:

1. transport ingress peeks the fixed header
2. classify whether the frame is:
   - local-only
   - local-and-forwarded
   - control-plane
   - forward-only
3. for forward-only traffic:
   - if exactly one downstream transport is interested, try immediate raw
     downstream relay
   - if downstream accepts, bypass ingress buffering entirely
   - if downstream backpressures, fall back to buffering the raw frame in
     ingress and retry later
   - if more than one downstream transport is interested, keep using the
     buffered path for now
4. for control-plane or locally consumed traffic:
   - continue using ingress buffering

This matches the intended physical model well enough for SPI and RS485 without
introducing fragmentation.

### Physical Plausibility Note

This next step is still physically realistic.

- it does not require PDU fragmentation
- it works with whole-frame forwarding
- it maps well to a future bus scheduler that decides whether a queued PDU fits
  into the current transport window
- it does not commit the codebase to a transparent multipath network protocol

For later wire protocols:

- whole-frame fit / budget decisions can still be made using cached encoded
  size or airtime metadata
- the bridge may still choose a stricter policy than the pure SPI router

## Why The Next Step Is Safe

Reasons this should not paint the design into a corner:

- ingress buffering remains available as the fallback path
- future fragment assembly can still be introduced at or below ingress later
- bridge C can keep stricter behavior than pure same-media routers if needed
- whole-frame relay does not require a transparent network protocol
- end consumers can still discard on CRC failure
- backpressure can still fall back to ingress buffering instead of forcing a
  brittle all-or-nothing fast path

Important boundary:

- do not force the SPI<->RS485 bridge to use the same fast path policy as the
  pure SPI router if later wire-level work shows different needs

## Main Open Question For Next Implementation

The key implementation question is how to add direct forward-only relay without
large interface churn.

The previously explored options were:

1. add a second raw-frame forwarding abstraction beside `Envelope`
2. keep `Envelope` and `IngressBuffer`, but defer validation and reuse raw bytes
3. add a selective bypass around ingress buffering for forward-only traffic

The recommended path is now:

- build on option 3
- keep the current `Envelope` path intact for local delivery
- add a direct whole-frame relay attempt before ingress buffering only when the
  frame is forward-only

## Proposed Next Fast Path

This is the currently preferred shape.

### Classification

At transport ingress:

1. peek the fixed header
2. decide whether the frame is:
   - control-plane
   - locally consumed
   - local-and-forwarded
   - forward-only

### Forward-Only Path

If the frame is forward-only:

1. attempt direct whole-frame relay to the required downstream transports
2. if all mandatory downstream queues accept it:
   - do not retain it in ingress
   - do not decode payload
   - do not run it through the normal buffered drainer path
3. if downstream backpressures:
   - fall back to the existing raw ingress buffering path
   - retry later using the buffered machinery

### Local / Control-Plane Path

If the frame is locally consumed, local-and-forwarded, or control-plane:

- keep the current buffered path
- continue using raw-byte retention and lazy validation where applicable

### Partial Fanout Rule

Preferred policy:

- avoid partial direct multicast fanout if possible
- if the fast path cannot complete cleanly, buffer once and retry from ingress

This keeps semantics conservative while preserving the forward-only fast path.

## Recommended Next Implementation Sequence

### Step 1: Add a Cheap Local-Interest Query

Goal:

- determine whether a topic has any local subscribers without iterating the full
  subscriber directory on every ingress frame

Likely path:

- use `SubscriptionManager::subscribed(topic)` as the local-interest signal
- ensure this remains aligned with actual local subscriber registration

Watch:

- `subscribed(topic)` currently tracks local subscription state, which is
  appropriate for this decision

### Step 2: Extend The Direct Relay Attempt At Transport Ingress

Goal:

- decide whether the conservative single-downstream fast path should be widened
  after fresh runtime measurements

Possible touch points:

- `include/PubSubBackend/detail/Drainer.hpp`
- `include/PubSubBackend/detail/Publisher.hpp`
- `include/PubSubBackend/Transports/BaseTransport.hpp`
- `include/PubSubBackend/Transports/LocalSharedBusRouterTransport.hpp`
- `include/PubSubBackend/detail/Node.hpp`

Important requirement:

- if direct relay cannot complete due to downstream backpressure, fall back to
  raw ingress buffering of the same frame
- do not allow partial multicast duplication when broadening beyond the current
  single-transport rule

Most likely touch points:

- `BaseTransport::send()`
- `Drainer::pollInto()`
- transport ingress callback plumbing in `Types.hpp`
- possibly `Publisher::dispatchFor()` only if target calculation cannot be
  reused cleanly

### Step 3: Make Partial Fanout Semantics Explicit

Goal:

- define what happens if a multicast direct relay can reach some downstream
  peers but not all

Preferred rule:

- avoid inconsistent partial direct relay if possible
- if the path cannot complete cleanly, buffer once and retry from ingress

If exact atomicity is too expensive:

- allow partial delivery only for noncritical traffic
- keep critical traffic conservative

### Step 4: Keep Bridge Policy Explicit

Goal:

- allow bridge C to opt out of the direct relay fast path later if RS485
  scheduling or validation constraints demand it

Do not:

- bake “all transports behave like SPI” into generic PubSub logic

### Step 5: Re-measure CPU / Latency

Success criteria:

- lower CPU usage on PubSub tasks
- no regression in correctness metrics
- no return of receipt/pool/egress timeouts
- lower completion latency, especially for forwarded traffic

## Implementation Guardrails

Keep these constraints active while coding:

- do not redesign `ByteArena`
- do not introduce fragmentation or reassembly yet
- do not replace `Envelope` globally
- do not force bridge and router to share identical future policies
- do not remove buffered ingress fallback
- do not assume ingress storage implies CRC validation
- do not reintroduce manual vtables
- prefer minimal diffs over broad interface churn

## Current Known Build State

At the time of this handoff:

- `pio run -e master` passes
- the latest successful build reported:
  - RAM: `45.3%` (`148452 / 327680`)
  - Flash: `38.2%` (`776061 / 2031616`)
- if PlatformIO truncates diagnostics, inspect `build/<env>/sarif/`

## Current Known Good Build / Runtime State

Before the next direct-bypass step:

- `pio run -e master` passes
- routing correctness was restored
- latest reported harness run had:
  - `attempts=260`
  - `pending=0`
  - `complete=260`
  - `receiptTimeout=0`
  - `poolTimeout=0`
  - `egressTimeout=0`
  - `targetMiss=260`
- interpretation:
  - delivery completeness had recovered for that run
  - latency compliance had not
  - effectively `100%` of completed packets were still over the `10ms` target
- remaining issue was CPU usage and completion latency still far above spec
  target

## Commands

Use for verification:

```bash
pio run -e master
```

Use compiler diagnostics if PlatformIO truncates errors:

```bash
rg -n "error" build/master/sarif/*.sarif
sed -n '1,220p' build/master/sarif/*.sarif
```

## Files Most Relevant For The Next Step

- `include/PubSubBackend/detail/Types.hpp`
- `include/PubSubBackend/detail/Drainer.hpp`
- `include/PubSubBackend/detail/Publisher.hpp`
- `include/PubSubBackend/detail/ControlPlane.hpp`
- `include/PubSubBackend/detail/SubscriptionManager.hpp`
- `include/PubSubBackend/detail/IngressBuffer.hpp`
- `include/PubSubBackend/detail/SerDe.hpp`
- `include/PubSubBackend/Transports/BaseTransport.hpp`
- `include/PubSubBackend/Transports/LocalSharedBusRouterTransport.hpp`
- `include/PubSubBackend/Transports/LocalSharedBusEdgeTransport.hpp`
- `src/master/main.cpp`

## Things To Avoid

- do not redesign `ByteArena`
- do not introduce fragmentation or reassembly yet
- do not replace `Envelope` globally
- do not force bridge and router to share identical future policies
- do not remove buffered ingress fallback
- do not assume ingress storage implies CRC validation

## Cold-Start Recovery Notes

If context compresses and work must resume from scratch, remember:

- the hard correctness bugs have already been fixed
- the system currently routes correctly enough to complete the harness with no
  receipt/pool/egress timeouts
- the remaining problem is CPU/latency, not missing subscriptions
- the current design direction is selective direct relay for forward-only
  traffic, with ingress fallback on backpressure
- the bridge must remain free to use stricter behavior later
- this next step should be treated as a bounded optimization, not as a network
  stack redesign

## Restart Checklist

When resuming after context loss:

1. read this document
2. read `docs/overview.md`
3. inspect:
   - `IngressBuffer.hpp`
   - `SerDe.hpp`
   - `BaseTransport.hpp`
   - `LocalSharedBusRouterTransport.hpp`
   - `Drainer.hpp`
   - `Publisher.hpp`
4. confirm that the current code still builds with `pio run -e master`
5. inspect `src/master/main.cpp` for the current harness timings and startup
   stagger values
6. confirm whether the current single-downstream direct-relay fast path is
   sufficient or should be widened
7. rebuild with `pio run -e master`
8. if diagnostics are truncated, inspect `build/<env>/sarif/`
