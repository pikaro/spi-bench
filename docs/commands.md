# Commands

These are the primary project commands currently used in practice.

## Build

- Build the active master target: `bin/build -e master`
- Build the active media SPI target: `bin/build -e media`
- Build the active GPU0 SPI target: `bin/build -e gpu0`
- Build the active GPU1 SPI target: `bin/build -e gpu1`
- Build the active IO RS485 target: `bin/build -e io`
- Build and emit a compilation database: `pio run -e master -t compiledb`
- Build LittleFS images for all active devices:
    `.venv/bin/pio run -e master -e media -e gpu0 -e gpu1 -e io -t buildfs`

Build output notes:

- Compiler diagnostics are emitted in SARIF format
- The firmware artifact for `master` is written under
    `build/master/firmware.elf`

## bin/build

- Thin wrapper around `pio run`
- Accepts repeated `-e`/`--environment` arguments and runs those environments
    sequentially, preserving ordinary PlatformIO arguments such as `-t upload`
- Moves compiler SARIF output under `build/<env>/sarif/`
- Build output and ESP-IDF component-manager state are per-environment under
    `build/<env>/`; the root-level `dependencies.lock` is obsolete

## Analyze object sizes

- `bin/objsizes master`: List object sizes for the `master` environment,
    including the names of the objects required for the `bin/objsize` command
- `bin/objsize master 'Metrics::backend()::instance'`: List sizes for all
    members of the `Metrics::backend()::instance` object
- `bin/task-stacks master`: List static task-storage symbols in the built
    firmware image and total their RAM cost
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
- Print repeated lines instead of the default sequential-identical-line
    suppression:
    `bin/monitor-multi --no-suppress-repeats --strip-ansi master`
- Capture matching lines with surrounding context:
    `bin/monitor-multi --strip-ansi --include ERR --context 2 master`
- Stop after a small number of printed lines:
    `bin/monitor-multi --strip-ansi --include RS485 --max-lines 5 master`
- Keep a full local log while printing filtered output:
    `bin/monitor-multi --strip-ansi --include WRN --log-file /tmp/master.log master`
- Collapse repeated matching lines:
    `bin/monitor-multi --strip-ansi --include 'SPI write failed' --summary master`
- Capture raw early boot/reset bytes without PlatformIO monitor filters:
    `bin/monitor-early master -d 15`
- Force ROM download-mode line states for diagnosis:
    `bin/monitor-early master --idle-dtr 0 --idle-rts 0 -d 3`

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

The timeout option accepts `ms`, `s`, and `m` suffixes. Sequential identical
terminal output lines are collapsed by default after normalizing ESP log
timestamp counters, while full logs and spool files still receive every line;
pass `--no-suppress-repeats` to print every line. `--include` and `--exclude`
may be repeated and match the ANSI-stripped line, including the
environment prefix only in multi-environment mode. `--before`, `--after`, and
`--context` print surrounding lines around matches. Use `--baud` for
non-default monitor baud rates in `bin/monitor-multi`; trailing positional baud
arguments are only supported by the `bin/monitor` compatibility wrapper.
`--upload` runs `pio run -e <env> --target upload` for every owned environment
before any monitor starts. The current per-environment build layout avoids the
old shared `dependencies.lock`/component-manager collision, so multi-env builds
and uploads are expected to be usable again. ESP32-C3/S3 USB and reset races can
still make a combined monitor pre-op fail without implying that the firmware
image is bad; when validating a tricky device issue, uploading the relevant
environment first and then attaching `bin/monitor-multi` remains easier to
reason about. `--reset` runs `pio run -e <env> --target reset` for every owned
environment after uploads and before any monitor starts; reset commands are run
in parallel. The reset target defaults to `upload_protocol`, but an environment
can set `custom_reset_protocol` when reset uses a different tool from upload,
such as nRF serial DFU upload with J-Link reset. nRF serial DFU bootloader entry
is a separate `reset-bootloader` target.
`--uploadfs` uploads the LittleFS image for every owned environment before
monitoring; for media WAV-source tests this image is built from
`data/media/littlefs`.
These options require owning the monitor for the requested environment; attached
follower instances cannot reset, upload, or upload filesystems.

When running multiple environments interactively, send a command to a specific
board with `!<env> <command>`, for example `!master /monitor`. If the current
instance is only attached to another instance's spool, the command is forwarded
through a per-environment command FIFO to the owner process.

`bin/monitor-early` is a direct pyserial capture tool for boot and reset
diagnostics. It bypasses `pio device monitor`, so PlatformIO exception-decoder
filters, reconnect behavior, and monitor banners cannot hide the raw serial
stream. It defaults to DTR high and RTS low, which boots the observed ESP32-S3
USB Serial/JTAG boards into the application. Releasing reset with DTR low enters
ROM download mode on the current master board, which is useful for confirming
that the tool is actually seeing boot bytes.

Runtime command discovery:

- List the commands currently registered on a node, including subcommands and
    arguments:
    `!master /help`

Useful LED animation commands:

- Publish animation commands over PubSub from any node:
    Optional animation arguments may be passed as `-` to keep their default
    value while setting later arguments.
    `!master /anim wave`
    `!master /anim wave 1200 144 180 2 2 5 1 1 4`
    Wave arguments are `durationMs`, `hue`, `value`, `rise`, `peak`, `wake`,
    `peakDelta`, `speedDelta`, `spokeModulo`.
    `peak` is the wave width. The delta arguments modulate the wave per spoke
    with a triangular modulo profile and default to no modulation. Passing
    `1 1 4` makes spokes `0..4` use offsets `0, 0.5, 1, 0.5, 0`, so with
    `peak = 2` the peak widths are `2, 2.5, 3, 2.5, 2`.
    `!master /anim starburst`
    `!master /anim starburst 1200 32 220 1 2 6 4 3 0 1`
    Starburst arguments are `durationMs`, `hue`, `value`, `rise`, `peak`,
    `wake`, `points`, `pointGain`, `twist`, `cycles`. It uses the same
    rise/peak/wake profile as the center wave, but `points` creates a
    triangular angular point profile and `pointGain` adds width and travel
    distance to the point spokes while low point-scale spokes fade out. `twist`
    adds ring-dependent angular phase; keep it `0` for straight burst points.
    `!master /anim vortex 2400 160 220 3 5 128 1 24`
    Vortex arguments are `durationMs`, `hue`, `value`, `arms`, `twist`,
    `width`, `cycles`, `hueStep`. Low `arms` values work best on 16 spokes.
    `!master /anim shutter 1600 48 210 8 128 48 1 1`
    Shutter arguments are `durationMs`, `hue`, `value`, `segments`, `openPct`,
    `edgeWidth`, `rotationCycles`, `mode`. Use `mode=0` for a fixed aperture
    edge and nonzero `mode` for opening/closing modulation.
    `!master /anim orbit 2400 96 220 128 36 28 2 1 56 220 24 48 1`
    Orbit arguments are `durationMs`, `hue`, `value`, `radius`, `radialWidth`,
    `angularWidth`, `comets`, `laps`, `trail`, `sparkle`, `hueJitter`,
    `radialDrift`, `radialDirection`. With a nonzero `trail`, the comet
    footprint is directional; `sparkle` randomizes trail brightness,
    `hueJitter` sets the per-pixel hue-deviation width, and `radialDirection`
    is `0` fixed, `1` outward, or `2` inward.
    `!master /anim lighthouse 3000 144 220 3 4 1 0 0`
    Lighthouse arguments are `durationMs`, `hue`, `value`, `beamWidth`,
    `trailSpokes`, `cycles`, `innerRing`, `outerRing`. `beamWidth` is clamped
    to at least `3` spokes on the 16-spoke surface to avoid split/aliased
    sweep heads. `outerRing=0` means the full strip.
    `!master /anim cymatic 3200 176 180 0 36 96 180 16`
    Cymatic arguments are `durationMs`, `hue`, `value`, `sourceMode`,
    `wavelength`, `speed`, `contrast`, `hueStep`.
    `!master /anim rings 2400 112 170 8 3 1 0 8`
    Rings arguments are `durationMs`, `hue`, `value`, `spacing`, `width`,
    `cycles`, `direction`, `hueStep`. Directions are `0` outward, `1` inward,
    and `2` in-place breathing.
    `!master /anim curtain 2600 200 190 4 32 128 0 16`
    Curtain arguments are `durationMs`, `hue`, `value`, `width`, `tilt`,
    `speed`, `outerOrigin`, `spokePhase`.
    `!master /anim lattice 2400 64 170 4 3 96 128 160`
    Lattice arguments are `durationMs`, `hue`, `value`, `radialMode`,
    `angularMode`, `speed`, `mix`, `contrast`. `speed` moves the radial phase;
    the angular lattice stays phase-stable.
    `!master /anim bolt 900 24 255 1 1 1 0 1`
    Bolt arguments are `durationMs`, `hue`, `value`, `width`, `jitter`,
    `forks`, `seed`, `outerOrigin`.
    `!master /anim sinelon`
    `!master /anim sinelon 0 96 120 3 2400 0 0 245 160 64`
    Sinelon arguments are `durationMs`, `hue`, `value`, `width`, `periodMs`,
    `outerOrigin`, `travelRings`, `bounceAttenuation`, `spokeGainPct`,
    `spokeGainPhaseStep`. Use `outerOrigin=1` to bounce inward from the outer
    edge, `travelRings=0` for the full strip depth, and
    `bounceAttenuation=255` for no per-bounce fade.
    `!master /anim sine`
    `!master /anim sine 3200 96 90 0 1 8 0 0 140 8 160`
    Sine arguments are `durationMs`, `hue`, `value`, `baseValue`, `width`,
    `wavelength`, `outerOrigin`, `travelRings`, `spokeGainPct`, `tailDecay`,
    `peakHold`, `lifetimeMs`. It scans a sine-valued head once over
    `durationMs` and renders the already-scanned trace directly on the effect
    layer.
    `tailDecay` is value loss per ring behind the head; `peakHold` reduces that
    loss for brighter sine peaks, with `0` meaning linear decay and `255`
    strongly preserving peaks. `lifetimeMs` is optional and defaults to the
    projected total time for the last peak to decay to the output value floor.
    `!master /anim weave`
    `!master /anim weave 0 144 220 20 2 3 1 180 96 40 192`
    `!master /anim weave 0 96 220 20 2 3 1 180 96 40 192 FftAlt`
    Spectral weave arguments are `durationMs`, `hue`, `value`, `baseValue`,
    `radialMode`, `angularMode`, `symmetry`, `contrast`, `peakSensitivity`,
    `flowSpeed`, `hueModulation`, `layer`. It starts the polar spectral weave
    field on the `Fft` layer by default; use `FftAlt` as the optional layer
    when staging a second FFT animation for a crossfade. `hueModulation`
    scales the audio-driven hue span; lower values keep the field near `hue`,
    while higher values allow broader multicolor output.
    `!master /anim iris`
    `!master /anim iris 0 96 220 8 8 128 28 180 96 24 128`
    `!master /anim iris 0 96 220 8 8 128 28 180 96 24 128 FftAlt`
    Spectral iris arguments are `durationMs`, `hue`, `value`, `baseValue`,
    `petals`, `aperture`, `rimWidth`, `contrast`, `peakSensitivity`,
    `flowSpeed`, `hueModulation`, `layer`. It renders an audio-reactive
    aperture, petal mask, and rim glow on the FFT layer.
    `!master /anim sparks`
    `!master /anim sparks 0 32 230 32 1 32 96 160 128 165 160`
    `!master /anim sparks 0 32 230 32 1 32 96 160 128 165 160 FftAlt`
    Orbit sparks arguments are `durationMs`, `hue`, `value`, `sparkCount`,
    `sparkSize`, `orbitSpeed`, `radialDrift`, `highSparkle`,
    `peakSensitivity`, `seed`, `hueModulation`, `layer`. It renders sparse
    FFT-driven embers and relies on FFT layer decay for trails.
    `!master /anim cells`
    `!master /anim cells 0 160 210 10 6 28 64 16 180 96 61 144`
    `!master /anim cells 0 160 210 10 6 28 64 16 180 96 61 144 FftAlt`
    Stained cells arguments are `durationMs`, `hue`, `value`, `baseValue`,
    `seedCount`, `borderWidth`, `interiorValue`, `driftSpeed`, `contrast`,
    `peakSensitivity`, `seed`, `hueModulation`, `layer`. It renders a small
    seed-count membrane field with audio-brightened borders and interiors.
    The default master orchestration starts a persistent FFT visual after
    startup, rotates through weave, iris, sparks, and cells with local layer
    crossfades, and periodically refreshes the active request. A stop-all
    command or a stop for either managed FFT request disables those refreshes
    until the master restarts.
    `!master /anim sweep`
    `!master /anim sweep 6000 0 220 0 1 255`
    Sweep arguments are `durationMs`, `baseHue`, `value`, `trailSpokes`,
    `cycles`, `markerValue`. Use zero trail for topology bring-up checks.
    `!master /anim wheel`
    `!master /anim wheel 0 160 96 3 1 1`
    Wheel arguments are `durationMs`, `hue`, `value`, `spokes`, `falloff`,
    `requestId`. The default request ID is the persistent wheel indicator ID
    used by wheel updates.
    `!master /anim wheel-update 160 96 3 1 1`
    Wheel-update arguments are `hue`, `value`, `spokes`, `falloff`,
    `requestId`.
    `!gpu0 /anim stop`
- Publish global LED display controls over PubSub:
    `!master /disp hue 32`
    `!master /disp rot 90`
    `!master /disp brightness 96`
    Hue offset is the raw `Angle<uint8_t>` value, 0-255. Rotation offset is in
    degrees; with the current 16 spokes, about 23 degrees advances one spoke.
    Brightness is the FastLED global brightness value, 0-255.
    Automatic master runtime orchestration is gated until the bring-up spoke
    sweep has finished. Manual commands remain direct diagnostics.
    Master no longer publishes center waves for every peak event. Peak events
    feed the active FFT visual and IO bulb flicker; after at least two seconds
    with no peak in any band, the first following peak publishes one short
    center wave as a primitive drop marker.
- Publish LED layer controls over PubSub:
    `!master /layer active FftAlt on`
    `!master /layer opacity FftAlt 0`
    `!master /anim weave 0 96 220 20 2 3 1 180 96 40 192 FftAlt`
    `!master /anim iris 0 96 220 8 8 128 28 180 96 24 128 FftAlt`
    `!master /layer swap Fft FftAlt 10000`
    `!master /layer opacity Fft 128`
    `!master /layer opacity FftAlt 127`
    `!master /layer active Fft off`
    Layers are parsed by enum name, case-insensitively. Useful names are
    `Background`, `Fft`, `FftAlt`, `Effect`, `TransientEffect`, `Wheel`, and
    `Debug`. Disabling a layer clears it once, then skips its per-frame
    clear/decay and compose work. Opacity is a layer compose alpha, 0-255.
    `/layer swap <Layer> <Layer> <durationMs>` starts a GPU-local fade between
    two layers and requires one layer at opacity 255 and the other at opacity
    0. The currently full-opacity layer must be active. At the end of the fade,
    the faded-out layer is disabled and animations still running on that layer
    are stopped.
    Master startup publishes per-layer active commands from
    `src/master/orchestration.hpp`; by default it disables `Background`,
    `FftAlt`, and `Wheel`.

Useful filesystem validation command:

- List the mounted LittleFS contents on a board:
    `!master /ls`
- List a subdirectory:
    `!master /ls /path`
- Dump a bounded slice of a LittleFS file:
    `!master /cat /errors.log 0 4096`
- Remove a LittleFS file, for example to clear the error journal:
    `!master /rm /errors.log`
- Download a node's LittleFS partition and unpack it outside the upload tree:
    `bin/littlefs-download master`

`bin/littlefs-download` writes to
`data/<env>/downloaded-littlefs/<timestamp>/`, not `data/<env>/littlefs`, so
retrieved logs are not included in later `uploadfs` images. The tool keeps the
raw partition image as `image.bin`, attempts an `mklittlefs` unpack into
`files/`, and writes printable-string fallback extracts if the host unpacker
cannot mount the raw image.

Useful audio-source validation command:

- Start media FFT background calibration through the existing button-event
    PubSub path:
    `!master /calibrate-audio`
- Generate a short media-node WAV fixture:
    `bin/wavgen.py test.wav beat --sample-rate 16000 --duration-ms 12000 --bpm 100 --freq 100`

Useful host PubSub UDP bridge commands:

- Run the host UDP participant and expose a local Unix-domain socket for
    arbitrary local PubSub tools:
    `bin/pubsub-udp-peer --mcu-ip <master-ip> --bind-ip <host-ip> --local-socket /tmp/totem-pubsub.sock`
- Render local FFT, peak, and beat events from that socket:
    `bin/pubsub-audio-view --socket /tmp/totem-pubsub.sock`
    Press `c` in the viewer to publish the same calibration button event as the
    IO GPIO input (`Button` topic, `Pressed`, `PeripheralButton::Calibration`).
    The current IO placeholder input is active-high GPIO3 with pulldown; GPIO0
    is already the IO RS485 RX pin.
- Local socket clients use newline-delimited JSON. Subscribe to a topic mask:
    `{"op":"subscribe","topic":16}`
- Publish a raw payload to a topic:
    `{"op":"publish","topic":16,"traffic_class":0,"payload_hex":"0001"}`
- Forwarded events are generic PubSub JSON with `header` and `payload_hex`.
    Message-specific decoding belongs in the local consumer script, not in the
    UDP bridge.

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

- `make wire` and `make bindings` cache `idedata` under `build/<env>/`
- Generated output is written to `include/Generated/Wire/`

## Environment Notes

- `master`, `media`, `gpu0`, `gpu1`, and `io` are the active environments for
    the current hardware PubSub star loop.
- The high-speed SPI star leg uses `gpu0` and `gpu1` as peers on one physical
    bus; build both GPU environments when touching shared SPI or PubSub code.
- PubSub stress profiles are available as `*-cross-stress`, `*-spi-stress`,
    and `*-spi-fast-stress` variants for the same active environments.
- `slave` is not defined in the current `platformio.ini`

Use other environments only when the task explicitly involves that work
