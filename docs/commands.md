# Commands

These are the primary project commands currently used in practice.

## Build

- Build the active master target: `bin/build -e master`
- Build the active SPI slave/media target: `bin/build -e media`
- Build and emit a compilation database: `pio run -e master -t compiledb`

Build output notes:

- Compiler diagnostics are emitted in SARIF format
- The firmware artifact for `master` is written under
    `.pio/build/master/firmware.elf`

## bin/build

- Thin wrapper around `pio run`
- Deletes unnecessary SARIF files after output

## Analyze object sizes

- `bin/objsizes master`: List object sizes for the `master` environment,
    including the names of the objects required for the `bin/objsize` command
- `bin/objsize master 'Metrics::backend()::instance'`: List sizes for all
    members of the `Metrics::backend()::instance` object
- `bin/gdb master` is a wrapper which starts GDB with the correct target and
    firmware file for the `master` environment

## Generated Wire Code

Generate wire support code for the current PlatformIO environment:

- `make wire`
- `make bindings`

Override the environment when needed:

- `make wire PIO_ENV=master`
- `make bindings PIO_ENV=master`

Supporting commands:

- Generate only the compilation database: `pio run -e master -t compiledb`
- Generate PlatformIO env metadata for generators: `pio run -e master -t idedata`
- Remove generated wire output: `make wire-clean`

Implementation notes:

- `make wire` and `make bindings` cache `idedata` under `.pio/build/<env>/`
- Generated output is written to `include/Generated/Wire/`

## Environment Notes

- `master` and `media` are the active environments for the current SPI Clock
    and PubSub hardware loop
- `gpu0`, `gpu1`, and `io` are present but should be built only when the task
    involves those source roots
- `slave` is not defined in the current `platformio.ini`

Use other environments only when the task explicitly involves that work
