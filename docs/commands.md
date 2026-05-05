# Commands

These are the primary project commands currently used in practice.

## Build

- Build the active master target: `bin/build -e master`
- Build the active media SPI target: `bin/build -e media`
- Build the active GPU0 SPI target: `bin/build -e gpu0`
- Build the active IO RS485 target: `bin/build -e io`
- Build and emit a compilation database: `pio run -e master -t compiledb`
- Build LittleFS images for all active devices:
    `.venv/bin/pio run -e master -e media -e gpu0 -e gpu1 -e io -t buildfs`

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
- Override monitor baud:
    `bin/monitor-multi --baud 115200 io`
- Reset or upload before monitoring:
    `bin/monitor-multi --reset master`
    `bin/monitor-multi --upload master`
- Upload a board's LittleFS image before monitoring:
    `bin/monitor-multi --uploadfs media`
- Capture a bounded, plain-text sample:
    `bin/monitor-multi --timeout 5s --strip-ansi master media`
- Capture only high-signal lines:
    `bin/monitor-multi --timeout 10s --strip-ansi --include 'WRN|ERR' master`
- Capture matching lines with surrounding context:
    `bin/monitor-multi --strip-ansi --include ERR --context 2 master`
- Stop after a small number of printed lines:
    `bin/monitor-multi --strip-ansi --include RS485 --max-lines 5 master`
- Keep a full local log while printing filtered output:
    `bin/monitor-multi --strip-ansi --include WRN --log-file /tmp/master.log master`
- Collapse repeated matching lines:
    `bin/monitor-multi --strip-ansi --include 'SPI write failed' --summary master`

`bin/monitor` delegates to `bin/monitor-multi` and preserves the historical
`bin/monitor <env> [baud]` form. With one environment, monitor output is
unprefixed and interactive input is sent directly to that board. With multiple
environments, output is prefixed and interactive commands use the `!<env>`
routing syntax. The shared implementation lives under `bin/lib/monitor/`; the
`bin/monitor-multi` file is only the executable entry point.

`bin/monitor-multi` resolves owned devices with `bin/select_port.py` in
parallel, runs optional pre-ops, then starts `pio device monitor` directly
inside a pseudo-terminal. The first instance for an environment owns the serial
monitor and writes a per-environment spool file under `/tmp`; later instances
for the same environment attach to that spool and apply their own filters. A
single FIFO is not used for monitor output because multiple FIFO readers split
bytes instead of each receiving the full stream.

The timeout option accepts `ms`, `s`, and `m` suffixes. `--include` and
`--exclude` may be repeated and match the ANSI-stripped line, including the
environment prefix only in multi-environment mode. `--before`, `--after`, and
`--context` print surrounding lines around matches. Use `--baud` for
non-default monitor baud rates in `bin/monitor-multi`; trailing positional baud
arguments are only supported by the `bin/monitor` compatibility wrapper.
`--upload` runs `pio run -e <env> --target upload` for every owned environment
before any monitor starts. `--reset` runs `pio run -e <env> --target reset` for
every owned environment after uploads and before any monitor starts; reset
commands are launched as one stage so final boot timing is closely aligned.
`--uploadfs` uploads the LittleFS image for every owned environment before
monitoring; for media WAV-source tests this image is built from
`data/media/littlefs`.
These options require owning the monitor for the requested environment; attached
follower instances cannot reset, upload, or upload filesystems.

When running multiple environments interactively, send a command to a specific
board with `!<env> <command>`, for example `!master /monitor`. If the current
instance is only attached to another instance's spool, the command is forwarded
through a per-environment command FIFO to the owner process.

Useful GPU LED bring-up commands:

- Trigger the default center-to-edge LED animation on one GPU:
    `!gpu0 /ledwave`
- Trigger it on both active GPU nodes:
    `!gpu0 /ledwave`
    `!gpu1 /ledwave`
- Trigger a local primitive demo on a GPU without PubSub:
    `!gpu0 /ledprim Explosion`
    `!gpu0 /ledprim Twinkle`
    `!gpu0 /ledprim Fire`
- Publish animation commands over PubSub from any node:
    `!master /anim wave`
    `!io /anim prim Comet`
    `!gpu0 /anim stop`

Useful filesystem validation command:

- List the mounted LittleFS contents on a board:
    `!master /ls`
- List a subdirectory:
    `!master /ls /path`

Useful audio-source validation command:

- Generate a short media-node WAV fixture:
    `bin/wavgen.py test.wav beat --sample-rate 16000 --duration-ms 12000 --bpm 100 --freq 100`

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

- `master`, `media`, `gpu0`, `gpu1`, and `io` are the active environments for
    the current hardware PubSub star loop.
- The high-speed SPI star leg uses `gpu0` and `gpu1` as peers on one physical
    bus; build both GPU environments when touching shared SPI or PubSub code.
- `slave` is not defined in the current `platformio.ini`

Use other environments only when the task explicitly involves that work
