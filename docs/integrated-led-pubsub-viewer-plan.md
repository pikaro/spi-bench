# Integrated LED PubSub Viewer Plan

This plan tracks the implementation of a host-side development tool that
combines generated PubSub command forms, LED animation preview, and PubSub
publishing in one pygame/pygame_gui application.

The first dependency is generated Python wire models from `make wire`. Python
must not learn command fields by scraping examples or maintaining a second
hand-written schema.

## Current Implementation Status

As of the first implementation pass:

- `make wire` also emits `tools/wire_models_py/totem_wire/generated.py`.
- The generated Python registry includes wire models, enum descriptors, field
  metadata, literal defaults, PubSub `Topic`/`NodeId` constants, and generic
  little-endian encoders.
- Transparent single-`value` wrappers are encoded through their underlying
  value type. Expression defaults such as helper function calls are still not
  evaluated in Python.
- `tools/led_pubsub_viewer/` provides a dynamic event catalog, payload encoder,
  renderer JSON adapter, PubSub bridge connection manager, headless CLI, and
  pygame_gui shell.
- `bin/led-pubsub-viewer list-events`, `dump-event`, `payload-hex`, and
  `render-json` are available for non-GUI validation.

Open next steps:

- Add GUI hint metadata for sliders, tighter numeric ranges, and grouping.
- Add frame slider/playback controls around rendered traces.
- Add preset export/import once the core payload flow settles.
- Add generated or host-dumped defaults for C++ expressions that are not
  literal AST values.

## Goals

- Generate Python models for wire payloads as part of `make wire`.
- Build GUI forms from those models, including enum dropdowns, boolean
  controls, numeric controls, defaults, and bounded ranges.
- Generate renderer JSON matching the existing `tools/led-render/examples/*.json`
  shape for renderable LED animation commands.
- Render and preview LED animations in the same pygame_gui window.
- Publish accepted commands over the existing UDP PubSub bridge.
- Let the same event selector create arbitrary supported PubSub commands, with
  preview disabled when the selected command is not renderable.
- Keep connection status useful for a dev tool without pretending it is a
  definitive distributed-system health check.

## Non-Goals

- Do not replace the C++ host renderer with Python animation logic.
- Do not make every local visualization script a separate UDP peer. The existing
  bridge remains the host PubSub participant.
- Do not require hardware for renderer preview or payload-generation tests.
- Do not add a new persistent PlatformIO environment.
- Do not hand-maintain a parallel schema for wire structs, enum values, or
  animation config fields.

## Existing Pieces

- `make wire` currently generates C++ wire field metadata under
  `include/Generated/Wire/`.
- `tools/led-render` renders production animation code from JSON to `.tled`
  traces.
- `tools/led_render_py/led_render` exposes `run_render`, `Trace`, analysis, and
  `viewer.frame_image()`.
- `tools/pubsub_udp_peer` wraps the C++ UDP peer and exposes a Unix-domain local
  socket that accepts raw `topic`, `traffic_class`, and `payload_hex` publishes.
- `tools/pubsub_audio_viewer` already proves the local socket client pattern,
  but its Python wire support is hand-written and audio-specific.

## Target Shape

```text
make wire
  -> include/Generated/Wire/*.hpp
  -> tools/wire_models_py/totem_wire/generated.py

integrated GUI
  -> generated model registry
  -> event catalog and GUI hints
  -> renderer JSON adapter for renderable LED animation commands
  -> bin/led-render -> Trace -> frame_image() -> pygame surface
  -> local PubSub socket publish(topic, traffic_class, payload_hex)
```

## Phase 1: Python Wire Models

Extend the existing wire generator path rather than adding a second discovery
tool. `make wire` should emit both the current C++ headers and a generated
Python package.

Proposed generated path:

```text
tools/wire_models_py/totem_wire/generated.py
tools/wire_models_py/totem_wire/__init__.py
```

The generated package should include:

- a registry keyed by C++ qualified name
- enum models with names and integer values
- wire struct models with fields in C++ wire order
- field metadata: name, type, scalar width, enum reference, nested model
  reference, array length, and default value
- Python dataclasses or model classes for values
- generic encode/decode helpers matching `PubSubBackend::detail::Codec`
- constants for `Totem::Data::PubSub::Topic` and `NodeId`
- an unsupported marker for fields whose type cannot yet be represented safely

Encoding rules must match the C++ codec:

- little-endian integral scalars
- enum values encoded using their underlying type
- `bool` encoded as one byte
- `std::array` and C arrays encoded element-by-element
- nested `WIRE_MSG` structs encoded field-by-field
- transparent value wrappers encoded as their single `value` member when the
  generator can prove the wrapper shape

Default values are part of the model contract. Start by extracting literal
defaults from the AST. If any active command payload depends on a C++ expression
that cannot be evaluated correctly in Python, add a tiny generated host-side
default dumper before building the GUI. Do not paper over missing defaults in a
manual Python table.

Initial acceptance checks:

```sh
make wire PIO_ENV=master
python3 -m py_compile tools/wire_models_py/totem_wire/generated.py
```

Add focused round-trip tests for representative payloads:

- `Totem::LedDisplay::AnimationCommand`
- one simple animation config such as `CenterWaveConfig`
- one config with bools or wider fields such as `SineWaveConfig`
- one nested/wrapper-heavy command such as `LedPwm::CommandEvent`

## Phase 2: Event Catalog and GUI Schema

The generated wire model says what can be encoded. A small source-controlled
event catalog should say which payload model is publishable on which PubSub
topic, and which commands are renderable.

The catalog should be narrow metadata, not a duplicate schema:

```text
label: "Animation / Play CenterWave"
topic: Topic.Animation
traffic_class: Noncritical
payload_model: Totem::LedDisplay::AnimationCommand
payload_template:
  type: Play
  kind: CenterWave
  layer: TransientEffect
config_model: Totem::LedDisplay::Animations::CenterWaveConfig
renderable: true
```

Use generated hard ranges from field types. Add a separate GUI-hints overlay for
developer ergonomics only:

- slider ranges tighter than the integer storage type
- step sizes
- labels
- grouped fields
- preferred control type when several are valid

Examples:

- hue, saturation, value: `0..255`
- layer: enum dropdown
- booleans: checkbox
- duration fields: numeric entry or slider with a practical upper bound
- raw payload bytes: hex text only as an escape hatch

## Phase 3: Integrated pygame_gui App

Add a new app rather than expanding the existing analysis viewer loop.

Proposed paths:

```text
tools/led_pubsub_viewer/
bin/led-pubsub-viewer
```

Main UI regions:

- connection bar: MCU IP, bind IP, connect/disconnect button, status indicator
- event selector: all cataloged PubSub commands
- command form: generated controls for the selected payload/config
- JSON/payload preview: renderer JSON for renderable commands and payload hex
  for publishable commands
- render controls: render, frame slider, play/pause, preview brightness, layout
- publish controls: accept/publish, dry-run payload generation, last publish
  result

Reuse `led_render.viewer.frame_image()` for the preview surface. Do not call
`led_render.viewer.play()` from inside the integrated tool because it owns its
own pygame event loop.

## Phase 4: Renderer Adapter

Renderable commands are LED animation commands that can be expressed as the
existing host-render JSON shape.

For `AnimationCommand` with `type=Play` and a concrete animation `kind`, build:

```json
{
  "animation": "CenterWave",
  "duration_ms": 1200,
  "layer": "TransientEffect",
  "frames": "0:180",
  "mode": "pipeline",
  "config": {}
}
```

The adapter should:

- fill `config` from the generated model value
- map `lifetimeMs` to root `duration_ms`
- map `layer` to the root `layer`
- carry preview-only controls such as frames, mode, brightness, layout, and
  synthetic inputs outside the wire payload
- disable preview for display controls, layer controls, IO LED PWM commands,
  audio/button events, and raw arbitrary payloads

The render path should write temporary config JSON and temporary `.tled` traces,
then load the trace with `Trace`. Rendering can be explicit at first. Add
debouncing later only if the manual render step feels too slow.

## Phase 5: PubSub Connection and Publish

Use the existing bridge path.

First implementation:

- spawn or manage `bin/pubsub-udp-peer --mcu-ip <ip> --local-socket <tmp-socket>`
- connect the GUI through `LocalPubSubClient`
- publish generated payloads through the local socket
- keep one bridge per GUI instance

Connection status should be pragmatic:

- disconnected: no bridge process or socket
- starting: bridge process launched, socket not connected yet
- bridge-local: local socket connected
- traffic-seen: keepalive, stats, or any PubSub event observed recently
- error: subprocess exit, socket failure, publish failure, or malformed status

This is enough for a development tool. It does not need to prove that every GPU
node received or applied a command.

Later improvements:

- attach to an already-running local socket
- show last stats from the C++ peer
- subscribe to the Animation topic and show observed command echo if useful

## Phase 6: Validation

Generator validation:

```sh
make wire PIO_ENV=master
python3 -m py_compile tools/wire_models_py/totem_wire/generated.py
```

Renderer validation:

```sh
bin/led-render --config tools/led-render/examples/center-wave-green.json \
  --output /tmp/center-wave.tled
bin/led-analyze summary /tmp/center-wave.tled
```

GUI dry-run validation:

- launch without hardware
- select each renderable animation command
- verify controls use generated defaults
- render at least one frame
- verify payload hex generation succeeds
- verify non-renderable commands disable preview but still show payload hex

Hardware validation:

- start master with UDP enabled
- connect using the GUI IP field
- publish a low-risk command such as a short `CenterWave`
- confirm runtime behavior through LEDs or monitor logs

## Iteration Order

1. Extend `make wire` to emit Python models and codecs.
2. Add tests or smoke checks for generated Python encoding.
3. Add the event catalog and GUI schema adapter.
4. Add the basic pygame_gui shell and generated command form.
5. Add renderer JSON generation and in-window preview.
6. Add PubSub bridge management and publish.
7. Add quality-of-life controls: export JSON, load/save presets, recent IPs,
   and optional attach-to-existing bridge.

Each step should leave existing renderer, viewer, and PubSub bridge entry
points working independently.
