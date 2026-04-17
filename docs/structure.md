# Repo Structure

This document records the repository-level layout and boundaries that are useful
for navigation and safe edits. Use MCP/LSP for symbol details inside these
areas.

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
- The wire output is produced by `bin/generate_wire_fields.py`
- Prefer regenerating these files through the documented command flow instead of
  editing generated output directly

## Editing Guidance

- Prefer changes in the owning subsystem instead of cross-cutting edits
- Treat `include/Base/` and similar foundational areas as high-impact; change
  them only when the task requires it
- Keep project docs focused on intent and workflow, not symbol duplication
