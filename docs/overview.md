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

Meaningful verification currently means:

- the relevant build passes
- the changed code path is reviewed in context
- the result looks correct at first glance for the intended embedded use

If stronger verification is required for a task, that should be requested
explicitly.

## Documentation Boundary

- Prefer MCP/LSP for up-to-date symbol, file, and call-structure discovery
- Use `docs/` for project intent, active scope, workflows, and decisions that
  are not reliably inferable from code alone
