# Project Overview

This repository contains an embedded C++23 library and application stack for
ESP32-class microcontrollers, built with PlatformIO on top of ESP-IDF.

The intended end state is a multi-device system composed of multiple
microcontrollers running code from this repository, coordinated through the
PubSub system while each device executes different components and
configurations.

## Current Scope

- `env:master` is the active PlatformIO environment and the default target for
    day-to-day work.
- Other PlatformIO environments currently serve as templates and placeholders
    for upcoming multi-device PubSub testing.
- When a task does not explicitly mention multi-device work, assume `master` is
    the only target that must remain buildable.

## Design Intent

- Platform-agnostic embedded abstractions where practical
- Header-heavy organization with small focused components
- Preference for deterministic behavior and low runtime overhead
- No desktop test harness at present; correctness is currently validated by
    successful compilation and careful code review in context

## Architectural Shape

Use MCP/LSP for symbol-level exploration. At a high level, the codebase is
organized as follows:

- `include/Services/`: public service entry points such as logging, metrics,
    commands, and PubSub
- `include/Base/`: reusable mixins and foundational capabilities shared across
    components
- `include/*Backend/`: subsystem implementations and internal machinery for
    commands, metrics, PubSub, task control, and related services
- `include/StaticConfig/`: compile-time configuration surfaces
- `include/Platform/` and `include/*/detail/platform/`: platform selection and
    platform-specific implementations
- `include/Generated/Wire/`: generated wire-format support code
- `src/master/`, `src/slave/`, `src/listener/`: environment-specific execution
    roots selected by build configuration
- `bin/`: project helper scripts used during build and code generation

## Build Model

- `platformio.ini` defines PlatformIO environments and board-specific flags
- Top-level `CMakeLists.txt` requires `SRC_ROOT` and maps it to the selected
    source subtree
- `src/CMakeLists.txt` maps PlatformIO environments to source roots:
    - `master` -> `src/master`
    - `listener` -> `src/listener`
    - `slave1` and `slave2` -> `src/slave`

## Verification Model

There is currently no unit-test or host-side simulation harness.

`src/master/main.cpp` now includes an on-device PubSub integration harness that
tracks expected recipients, message-pool release, and transport egress release
for the active test topology. The harness waits for a configured warm-up window
before publishing test traffic so subscription replay and simulated transport
boot readiness do not pollute steady-state latency measurements.

PubSub node runners keep their periodic task loop, but can now be woken early
through the existing notify mechanism. Local publish and local test-transport
ingress use that wake path so work can run before the next scheduled poll while
the periodic cadence remains in place for watchdog coverage and baseline
polling. The on-device harness disables PubSub runner auto-restart so task
failures remain visible during latency and pressure testing instead of being
converted into repeated stack allocations.

The local shared-bus simulation treats peer readiness as a bus-level event:
when a peer becomes available, the router wakes its owning PubSub node so
subscriptions can be replayed to peers that missed earlier control-plane
advertisements.
Router fanout is synchronous in the local shared-bus transport: enqueueing a
frame clocks it into target peer queues immediately instead of staging an extra
router egress dispatch. This keeps the single-device simulator closer to the
intended low-utilization SPI bus and avoids measuring an artificial router task
queue as bus latency.
Frames received from transport ingress now retain their serialized wire image
after a fixed-header peek, so forwarding transports can reuse the original
bytes instead of reserializing the same envelope on every hop. Full frame CRC
validation is deferred until a local payload read actually occurs, which keeps
forward-only traffic closer to cut-through behavior while preserving end-to-end
integrity checks for consumers.
Transport ingress now also has a selective direct-relay fast path: if a
received frame is neither control-plane nor locally subscribed, and exactly one
downstream transport is interested, the node can hand the raw wire bytes
straight to that downstream transport without first retaining the frame in the
local ingress buffer. If the direct handoff backpressures or routing is more
complex, the existing buffered ingress path remains the fallback.

Task monitoring treats managed task-controller runners and platform/system tasks
as distinct sources. Managed native task handles are tracked explicitly so the
system task source skips those tasks instead of reporting duplicates.

Meaningful verification currently means:

- the relevant build passes
- the changed code path is reviewed in context
- the result looks correct at first glance for the intended embedded use

Service facades are intended to stay lightweight. In particular, `Services/*`
headers should not pull full backend implementations into unrelated subsystems.

Logging egress over UART now uses the ESP-IDF TX software buffer configured via
`include/StaticConfig/Uart.hpp`, and the logging sink no longer forces a full
UART drain after every record by default. This keeps logger callers decoupled
through the aggregator ring buffer while letting the UART driver absorb bursty
output without stalling the emitter task on each line.

If stronger verification is required for a task, that should be requested
explicitly.

## Documentation Boundary

- Prefer MCP/LSP for up-to-date symbol, file, and call-structure discovery
- Use `docs/` for project intent, active scope, workflows, and decisions that
    are not reliably inferable from code alone
