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

## Monitor

- Monitor one environment: `bin/monitor master`
- Monitor multiple environments with prefixed output:
    `bin/monitor-multi master media`
- Reset or upload before monitoring:
    `bin/monitor-multi --reset master`
    `bin/monitor-multi --upload master`
- Capture a bounded, plain-text sample:
    `bin/monitor-multi --timeout 5s --strip-ansi master media`
- Capture only high-signal lines:
    `bin/monitor-multi --timeout 10s --strip-ansi --include 'WRN|ERR' master`
- Stop after a small number of printed lines:
    `bin/monitor-multi --strip-ansi --include RS485 --max-lines 5 master`
- Keep a full local log while printing filtered output:
    `bin/monitor-multi --strip-ansi --include WRN --log-file /tmp/master.log master`
- Collapse repeated matching lines:
    `bin/monitor-multi --strip-ansi --include 'SPI write failed' --summary master`

`bin/monitor-multi` allocates a pseudo-terminal for each PlatformIO monitor so
PlatformIO's interactive serial monitor can start in the background. The first
instance for an environment owns the serial monitor and writes a per-environment
spool file under `/tmp`; later instances for the same environment attach to that
spool and apply their own filters. A single FIFO is not used for monitor output
because multiple FIFO readers split bytes instead of each receiving the full
stream.

The timeout option accepts `ms`, `s`, and `m` suffixes. `--include` and
`--exclude` may be repeated and match the prefixed, ANSI-stripped line.
`--reset` and `--upload` pass `RESET=true` and `UPLOAD=true` respectively to
owned `bin/monitor` subprocesses, matching the environment-variable behavior of
`bin/monitor`. These options require owning the monitor for the requested
environment; attached follower instances cannot reset or upload.

When running interactively, send a command to a specific board with
`!<env> <command>`, for example `!master /monitor`. If the current instance is
only attached to another instance's spool, the command is forwarded through a
per-environment command FIFO to the owner process.

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
