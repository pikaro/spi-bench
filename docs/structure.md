# Repo Structure

This document records the repository-level layout and boundaries that are useful
for navigation and safe edits. Use MCP/LSP for symbol details inside these
areas.

## Compiler diagnostics

- `main.cpp.cpp.sarif` in the project root may contain compiler diagnostics in
    SARIF format when a build or IDE integration produced one. Treat it as a
    diagnostic artifact to inspect when command-line output is truncated, not as
    authoritative source state.

## Top-Level Layout

- `include/`: reusable library code and subsystem internals
- `src/`: executable entry points grouped by deployment role
- `docs/`: project-level guidance, scope, and workflow notes
- `bin/`: build helpers and code-generation scripts
- `boards/`: custom PlatformIO board definitions
- `platformio.ini`: active build environments and common flags
- `CMakeLists.txt` and `src/CMakeLists.txt`: source-root selection and ESP-IDF
    component registration

## Source Roots

- `src/master/`: current primary execution target
- `src/slave/`: shared source root for slave-oriented environments
- `src/listener/`: listener-oriented execution target

The source root used in a build is selected through `SRC_ROOT`, which is driven
by the current PlatformIO environment.

## Generated Code

- `include/Generated/Wire/` contains generated wire support code
- `include/Generated/Bindings/` contains generated binding support code
- The generated outputs are produced through `make wire` and `make bindings`
- Prefer regenerating these files through the documented command flow instead of
    editing generated output directly

## Component Boundaries

Components generally use three header boundaries:

- `Foo/Facade.hpp` is the curated public entry point. It selectively exports
    concrete public classes and aliases from the component without making every
    implementation header part of the public API.
- `Foo/Interfaces/` is a lightweight public surface for externally named types,
    config, handles, contracts, or interfaces that consumers need without
    including the full facade. Do not create this folder preemptively; small
    components may not need it.
- `Foo/detail/` contains implementation details, helpers, storage, schedulers,
    parsers, and component-owned platform glue. External code should not include
    detail headers unless it is intentionally working inside that component
    boundary.

Top-level shared interface folders exist when multiple components need the same
concept. For example, `include/Wire/Interfaces/` contains request/result and
payload types shared by wire implementations such as RS485 and future SPI.
Component-specific config can stay in `detail/` if callers only pass it inline
at construction, as RS485 currently does. Move such types to an `Interfaces/`
folder only when external code needs to name or store them independently.

`include/Wire/Spi/` currently follows that component-local shape: public
construction goes through `Wire/Spi/Facade.hpp`, externally instantiated
config/types live in `Wire/Spi/Interfaces/`, and the ESP32 SPI driver wrapper is
owned by `Wire/Spi/detail/platform/`. Do not move SPI into top-level
`Platform/` unless a second component starts using the same abstraction
directly.

Wire-level helpers that are reused across physical transports belong in
`include/Wire/detail/`. `Wire/detail/AttentionLine.hpp` is one such helper: it
owns the active-low open-drain GPIO attention-line behavior used by RS485 and
SPI while transport-specific code decides which side observes or drives it.

## Editing Guidance

- Prefer changes in the owning subsystem instead of cross-cutting edits
- Treat `include/Base/` and similar foundational areas as high-impact; change
    them only when the task requires it
- Keep project docs focused on intent and workflow, not symbol duplication
