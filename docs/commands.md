# Commands

These are the primary project commands currently used in practice.

## Build

- Build the active target: `bin/build -e master`
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

Override the environment when needed:

- `make wire PIO_ENV=master`

Supporting commands:

- Generate only the compilation database: `pio run -e master -t compiledb`
- Remove generated wire output: `make wire-clean`

Implementation notes:

- `make wire` uses `bin/generate_wire_fields.py`
- Generated output is written to `include/Generated/Wire/`

## Environment Notes

- `master` is the only actively maintained environment for routine work at
    present
- `listener`, `slave1`, and `slave2` remain as templates for upcoming
    multi-device PubSub testing

Use other environments only when the task explicitly involves that work
