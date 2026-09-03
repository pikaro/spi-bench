# Project Overview

This repository contains an embedded C++23 library and application stack for
ESP32-class microcontrollers, built with PlatformIO on top of ESP-IDF.

The intended end state is a festival "rave stick": a reliable multi-node LED
controller that can run unattended for many hours. A bus master handles WiFi and
central coordination; a media node processes I2S audio and publishes FFT, peak,
and beat outcome events; four ESP32-S3 GPU nodes render LED frame segments; side
nodes over RS485, I2C, and BLE provide sensors, bulbs, and future peripherals.

The system is event-driven because full LED frame data is too expensive to
publish at the target 100 fps. Nodes publish compact events such as FFT frames,
peaks, beat outcomes, bell strikes, warnings, errors, and metrics. GPU nodes
subscribe to those events and render their own LED segment locally.

## Current Scope

- `env:master`, `env:media`, `env:power`, `env:gpu0`, `env:gpu1`, and `env:io`
    are active targets for the current hardware PubSub network. The master owns
    a low-speed SPI bus shared by Media and Power, a high-speed SPI bus shared
    by GPU0 and GPU1, and one RS485 link to IO.
- `env:ai` is an explicit standalone prototype target outside the current
    PubSub network. It runs the SPH0645 microphone through ESP-SR noise
    suppression, neural VAD, Alexa/Computer WakeNet models, and WakeNet AGC;
    the detected wake profile selects the assistant voice, wake/VAD session
    state drives its status LED, cleaned command PCM is uploaded through an
    authenticated WebSocket turn, and response PCM plays through MAX98357.
- `env:scratch` is temporarily an ESP32-S3 Zero wired as GPU1 for isolated LED
    output bring-up. Its throwaway application sends a hand-built flat-color
    SK9822 frame directly through SPI3, bypassing the display pipeline.
- `env:power` is the ESP32-C3 monitoring node. It is a low-speed SPI PubSub
    slave and I2C master for the 24 V INA226 at `0x40` and the common 5 V
    INA226 at `0x41`, both using 2 mOhm shunts. Only the 24 V monitor feeds the
    BatteryMonitor behavior; both monitors publish their normal INA metrics.
- Production node entrypoints use `include/Setups/PubSubNetwork.hpp`, which
    registers the real transports and exposes `PubSubService` without starting
    synthetic test publishers or subscribers. The synthetic multi-board
    regression harness remains in `include/Setups/PubSubStarTest.hpp`.
- Power owns the configurable WiFi runtime and UDP PubSub edge, bridging its
    active network mode to the low-speed SPI bus. Both AP and station
    credentials remain configured; `wifiConfig.mode` selects one, with AP mode
    active initially. Master is SPI/RS485-only because WiFi caused unacceptable
    SPI disruption under load on that timing-critical node.
- Hardware SPI now supports multiple logical master links sharing the same
    ESP32 SPI bus. The active high-speed bus uses one PubSub SPI router
    transport with GPU0 and GPU1 peers. The low-speed bus uses a second PubSub
    SPI router with Media and Power peers.
- Master, Media, and Power normally run their SPI transports and application
    services. An opt-in v2 SPI0 GPIO signal bring-up profile can disable those
    services and fingerprint every low-speed SPI net with a distinct PWM
    frequency. The reusable producer and edge-measuring consumer are documented
    in [gpio-signal-test.md](gpio-signal-test.md).
- GPU LED presentation is synchronized by a master-driven 100 Hz GPIO present
    strobe. The v2 master drives GPIO10; GPU0 receives it on GPIO10 and GPU1 on
    GPIO4. The production surface has 32 spokes with 60 LEDs each. GPU0 owns
    spokes 0..15 and GPU1 owns spokes 16..31, with one 960-pixel SK9822 chain
    per GPU.
- Each GPU remains an ESP-IDF SPI2 PubSub slave and drives its LEDs from the
  independent SPI3 master peripheral at 4 MHz. GPU1 GPIO9 owns
  the shared active-low 74AHCT125 output gate; a 10 kΩ hardware pull-up keeps
  both clock/data pairs isolated through reset. Each GPU initializes its local
  LED output and publishes readiness through PubSub. After master has received
  readiness from both GPUs, it publishes the shared output-enable command and
  then the boot animation. The gate is signal isolation, not display blanking.
- There is no `env:slave` in this checkout. Historical RS485 slave
    documentation may refer to that environment, but `platformio.ini` no
    longer defines it.
- When a task touches shared wire, PubSub, Clock, or platform abstractions,
    build `master`, `media`, `power`, `gpu0`, `gpu1`, and `io` unless the task is
    explicitly scoped to one environment.

## Design Intent

- Platform-agnostic embedded abstractions where practical
- Header-heavy organization with small focused components
- Preference for deterministic behavior and low runtime overhead
- Platform-independent host tests cover LED topology, ownership, SK9822
    encoding, and dense full/half trace reconstruction; hardware behavior still
    requires explicit bench validation

## Metrics Initialization

Subsystem metric accessors are hot-path accessors, not lazy constructors. Every
component with singleton-style metrics must call its `prewarmMetrics()` hook
during begin, after `metricsBackend.begin()` and before it starts tasks,
registers ISRs, subscribes PubSub callbacks, or enables transport activity.
Display-side audio metrics use the same rule through `prewarmDisplayMetrics()`.

Do not reintroduce function-local static metric initialization such as
`static Metrics instance = Metrics::create()` in accessors. On ESP32, first use
from ISR-heavy or multi-core paths can enter C++ guard initialization
(`__cxa_guard_acquire`) while another core is also touching the accessor. That
can stall timing-critical paths long enough to trip the interrupt watchdog.
Use the prewarmed accessor pattern from `include/Macros/internal/Metrics.hpp`
instead; `metrics()` should abort if a caller reaches it before the component
has explicitly prewarmed registration.

Metric counters and ordinary gauges use unsigned 32-bit values. Measurements
whose domain includes negative values use `SignedGauge` and
`SignedGaugeHandle`; they retain an `int32_t` bit pattern in the same four-byte
atomic storage used by unsigned metrics. Counters remain unsigned. Snapshot
sinks must inspect the metric descriptor before interpreting the raw value, or
use `Metric::signedValue()` for a signed gauge.

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
- `include/FileSystem/`: static-memory filesystem wrapper selected through the
    component detail layer; the current backend is ESP-IDF LittleFS mounted at
    `/littlefs`
- `include/SecretStorage/`: uncached byte-oriented key/value storage backed by
    the configured ESP32 NVS partition; `CoreSetup` binds it through
    `SecretService`, and typed reads require an exact stored size
- `include/Wifi/`: ESP32 WiFi lifecycle wrapper with bounded project-owned
    config, status reporting, and master command integration
- `include/Network/`: thin lwIP UDP/TCP socket wrappers and diagnostic commands
    for one-shot network probes
- `include/Bluetooth/`: fixed-capacity BLE central abstraction with an ESP-IDF
    NimBLE backend for node-local device profiles
- `include/DigitalInput/`: one-GPIO physical inputs with ISR edge handling,
    atomic ISR/poll reconciliation, optional stable-state debouncing, and rich
    edge callbacks suitable for semantic adapters and state machines
- `include/GpioSignalTest/`: configurable timer-driven GPIO PWM producer or
    interrupt-based frequency/duty/edge consumer for wiring and pin-matrix
    diagnostics; see [gpio-signal-test.md](gpio-signal-test.md)
- `include/Button/`: the thin active-polarity and pressed/released semantic
    adapter over `DigitalInput`; the concrete class owns a bounded inline
    callback instead of exposing its callback type in the class API, while
    optional `Behavior` classes add reusable gesture semantics
- `include/PubSubEventProducer/`: the shared ISR-safe event queue and task;
    producer-created callbacks enqueue compact factories/arguments and the task
    constructs and publishes the complete typed PubSub payload
- `include/RotaryEncoder/`: two-`DigitalInput` quadrature decoding with a
    compact transition table, plus an optional held-button menu behavior
    kept separate from the physical decoder
- `include/Wheel/`: BLE wheel device-profile driver and wire payload used by
    `io` to publish wheel rotation events
- `include/AudioSource/`: media-node compile-time selected I2S, LittleFS WAV,
    Bluedroid A2DP, or BTstack A2DP PCM input sources; see [audio.md](audio.md)
- `include/AudioSink/`: PCM output sinks for I2S, TCP, and WebSocket/WSS
    transports; see [audio.md](audio.md)
- `include/AudioAfe/`: ESP-SR model lifecycle, speech front-end processing,
    detector metadata, and AFE health metrics for the AI node; see
    [audio.md](audio.md)
- `src/ai/assistant_session.hpp` and `assistant_websocket.hpp`: the AI node's
    wake/VAD turn controller, bounded PSRAM capture path, authenticated WSS
    protocol client, and response playback; see the
    [assistant WebSocket plan](ai-assistant-websocket-plan.md)
- `include/AudioFft/`: FFT analysis, magnitude scaling, peak extraction,
    first-pass tempo tracking, wire payloads, and the media debug display; see
    [audio.md](audio.md)
- `include/LedTopology/` and `include/LedDisplay/`: GPU-node logical LED
    topology, compile-time ownership, compile-time-selected legacy FastLED or
    direct ESP-IDF SK9822 output, local animation playback, primitive drawing
    helpers, and PubSub animation commands; see
    [animation-pipeline.md](animation-pipeline.md)
- `include/Generated/Wire/`: generated wire-format support code
- `include/Wire/Rs485/`: point-to-point RS485 wire layer; see
    [wire-rs485.md](wire-rs485.md)
- `include/Wire/I2C/`: ESP32 I2C master bus, fixed-capacity device registry,
  and low-speed peripheral drivers including model-selected INA219/INA226
  power monitors; see [wire-i2c.md](wire-i2c.md)
- `include/BatteryMonitor/`: sensor-independent discharge integration, battery
  budget/TTE estimation, and CRC-protected LittleFS calibration profiles; see
  [battery-monitor.md](battery-monitor.md)
- `include/Wire/Spi/`: in-progress DMA-oriented SPI wire layer with a
    component-owned ESP32 platform abstraction; see
    [spi-transport-plan.md](spi-transport-plan.md)
- `src/master/`, `src/media/`, `src/power/`, `src/gpu/`, `src/io/`, `src/ai/`,
    and `src/scratch/`:
    environment-specific execution roots selected by build configuration.
    Both GPU PlatformIO environments currently map to `src/gpu/`.
- `src/master/orchestration.hpp`: master-local show orchestration. It
    subscribes to compact events such as wheel movement and emits existing LED
    animation commands; mappings are intentionally plain C++ config-as-code,
    not a shared semantic configuration layer.
- `src/master/led_bringup.hpp`: master-local LED bring-up probes. It publishes
    generic animation commands after boot so mapping checks and persistent
    indicator startup use the same path as runtime animation requests.
- `env:io` targets the ESP32-C3 SuperMini board variant in this repository and
    uses the 4 MiB flash layout under `partitions/esp32_4mib.csv`.
    Its runtime command console is the USB Serial/JTAG console; native UART0
    remains configured at 921600 baud for ESP-IDF early boot and panic output.
- `bin/`: project helper scripts used during build and code generation

Component header boundaries are described in [structure.md](structure.md):
`Facade.hpp` is the curated public entry point, `Interfaces/` is only for
lightweight public types that external code needs to name directly, and
`detail/` owns implementation internals.

### Status LED

`include/StatusLed/` provides the global RGB status LED component exposed
through `include/Services/StatusLed.hpp`. It is initialized from `CoreSetup`
before the long node setup delay where a node has configured WS2812B hardware,
shows cyan while booting, and blue after core setup. Nodes may select the
generic green target-ready state or replace blue with a node-local
informational state; AI uses dim-white `Listening`, explicit black `Off`, and
purple `Playback` instead of green. `Off` immediately clears the physical LED
without changing warning/error/critical masks. Consumers register named states
through the tiny `StatusLed::Directory` handle and flip the returned
`StateHandle`. Activating
an informational state replaces the prior informational state; active warning,
error, and critical conditions retain their masks and cycle at the highest
severity every 500 ms. `StatusLed::Config::brightness` linearly scales each RGB
channel before output and defaults to 30%. The current no-FastLED backend drives
one WS2812B with ESP-IDF RMT and only writes the retained LED color when the
selected RGB value changes.

Logging and status indication are intentionally separate. Error-level records
remain available for failures that are subsequently handled, retried, or
converted into normal behavior. `REPORT_IF_ERR` consumes a `ReturnCode` only at
an ownership boundary where it can no longer be handled or propagated and
latches the red `UnhandledError` state. The `ABORT_*` macros instead select the
higher-priority critical state immediately before terminating the system.

### IO Lighting

The current `env:io` board is an ESP32-C3 side node intended to sit inside the
treasure chest, separate from the main controller board. Its local `LedPwm`
setup exposes configured LED contexts for two external E27 bulbs and one
onboard gold LED. Public LED commands still require a returned `LedContext`, so
callers can only address LEDs present in the node configuration.
`PeripheralLedConfig::configured` marks populated static slots; unconfigured
slots are ignored so nodes can own fewer LEDs than `LedPwmConfig::maxLeds`
without creating dummy GPIO outputs. The media node uses the same `LedPwm`
component for the active-low GPIO44 FFT peak indicator.
The same node owns the ship's bell input as an active-high GPIO button with an
external pulldown, plus a separate calibration button. Each `Button` composes a
`DigitalInput` and only maps its electrical level to pressed/released semantics.
`DigitalInput` owns GPIO lifecycle, any-edge ISR registration, atomic ISR/poll
reconciliation, and optional trailing-edge debounce. The ESP32 platform installs
the process-global GPIO ISR service once, then registers one handler for each
configured input pin. Its callback retains pin, edge, level, source, and
timestamp metadata; with debounce disabled it runs directly in ISR or polling
context, while an enabled debounce emits the settled transition from `work()`.
`DigitalInput` and `Button` are concrete classes with
small allocation-free callback storage; only their constructors are templated
to validate and capture caller lambdas. IO button callbacks come from the
shared `PubSubEventProducer`: they queue the pressed/released event and a small
factory capture, after which the producer task attaches application button
identity and constructs `Data::ButtonEvent`. `inputs` and `inpDiag` report
physical transitions and ISR/poll/debounce activity; `buttons` and `btnDiag`
report semantic button callbacks and filtering. `evtCore`, `events`, and
`evtDiag` report producer queue drops/publish failures, successful production,
and ISR/task enqueue activity for every event source using the producer.

`Button::Event` distinguishes raw semantic transitions (`Pressed`, `Released`)
from complete gestures (`Press`, `LongPress`, `DoublePress`), and the wire
schema carries that enum without a duplicate application-specific event type.
`Button::Behavior::PressClassifier` consumes timestamped pressed/released
callbacks and owns no task or platform timer. Optional `longPressMs` and
`doublePressMs` timeouts are evaluated by `work(nowMs)` against a packed atomic
state, allowing button transitions to arrive from ISR while timeouts run in
task context. A long press suppresses the release-time press. When a second
press becomes long, the classifier preserves the pending first `Press` and
then emits `LongPress` for the second gesture.

`RotaryEncoder` composes two non-debounced, non-polled `DigitalInput` channels
because quadrature decoding depends on raw edge order. Its 16-entry Gray-code
transition table prevents alternating contact bounce from completing a detent,
restarts partial accumulation on direction changes, rejects skipped diagonal
states, and calls its owner with only `Clockwise` or `Counterclockwise` once per
configured detent. `PositionConfig` optionally bounds its counter and selects
its initial value; an increment at a bound is discarded without invoking the
callback. With no bounds the counter starts at zero. The hardware class has no
PubSub, brightness, or menu vocabulary.
`RotaryEncoder::Behavior::ButtonMenu` separately combines those direction
callbacks with a button's pressed/released callbacks. Rotation reaches the
bounded `Dial` behavior when the button is not held; while held, it updates an
atomic signed menu position and emits absolute `Shown`, `Moved*`, and `Selected`
snapshots. The IO node publishes those as distinct `Dial` and `Menu` topics and
keeps the rotary switch's pressed/released transitions local to `ButtonMenu`.
GPIO8 is not classified or published as an independent `ButtonEvent`, because
its application meaning is the radial menu's show/select lifecycle.
The IO encoder uses GPIO2 for CLK and GPIO3 for DT with pull-ups and raw
any-edge interrupts. Its active-low GPIO8 switch uses 20 ms debounce. The
brightness dial is bounded to `0..31` with initial position 16; the main menu is
bounded to `-3..4` with Reset at -3, Next at -1, Toggle at 0, Calibrate at 1,
Debug at 2, Battery at 3, and `None` in the other two slots. Selecting Next
starts the fade to the next FFT/background animation. Master consumes the main
dial's normalized `value` as the GPU display brightness and uses its exact
`position` to start a 500 ms full-white radial gauge. The power node publishes
a compact battery-status snapshot once per second. Selecting the main menu's
Battery item uses its latest fresh estimate to start the same gauge with a
red-to-green `0..100%` spectrum. `Shown` and `Moved*` events drive a
configurable radial menu on the topmost `UI` layer; `Selected` stops it before
dispatching the implemented action. UI pixels retained after an animation stops
decay over roughly two seconds.
Selecting `Debug`, or running the master-only `/debug` command, toggles the
master's debug mode. Its current start hook enables the `Wheel` layer and starts
the persistent full-brightness one-spoke Wheel animation; its stop hook stops
that animation and disables the layer. Future debug features extend those two
lifecycle hooks rather than adding another toggle state.

`LedPwm` separates direct electrical duty from human-oriented brightness:
`setDuty()` writes linear PWM duty and clears any active brightness animation
state, while `setBrightness()` establishes a persistent gamma-corrected base
brightness. Fire-and-forget animations are started with `startAnimation()` or
the convenience `pulse()` command. The handler owns a fixed set of static
animation slots per LED (`LedPwmConfig::animationSlots`, currently 10), steps
them from the `LedPwm` task, and applies the brightest value among the base
brightness and all active animation slots. `clearAnimations()` removes overlays;
send an explicit brightness or duty command afterward when a stop command
should also force the LED output off.

`Pulse` and `Glitter` are the current LED PWM animation payloads. Master-owned
orchestration publishes queue-copyable `LedPwm::CommandEvent` payloads over the
`LedPwm` PubSub topic, and `io` only maps those logical LED commands to local
PWM contexts. This keeps beat, bell, and chest-light animation policy on the
master while preserving `io` as the hardware execution node.
LED PWM command metrics live in `ledCore`, `ledPwm`, and `ledProf` for command
failures, queued/handled command counts, and opt-in task-step profiling.

## Build Model

- `platformio.ini` defines PlatformIO environments and board-specific flags
- PlatformIO build output is rooted at `build/<env>/`. ESP-IDF component-manager
    state is also per-environment there, including `dependencies.lock` and
    `managed_components/`, so different ESP targets do not rewrite shared root
    artifacts.
- LittleFS images are built from `data/<env>/littlefs` for each device
    environment and mounted during `CoreSetup`; boot logs recursively list the
    mounted contents for validation. The same listing is available at runtime
    through `/ls` with an optional path argument. First boot after changing an
    old flash partition to LittleFS may format the partition if stale contents
    cannot be mounted; a later mount/listing failure is logged without blocking
    the command console.
- Development builds control the LittleFS-backed logging error journal through
    constexpr knobs in `StaticConfig/Logging.hpp`, especially
    `LoggingConfig::errorJournalEnabled` and
    `LoggingConfig::errorJournalSlots`. The journal records first-seen error
    sites to `/errors.log` with fixed slots, recent context lines, and
    trailing lines queued directly to its writer task. It stays quiet during
    expected storage failures and disables itself if the filesystem is full.
- Runtime task statistics use ESP-IDF's 64-bit FreeRTOS runtime counter in
    `sdkconfig.stack.esp32`, because the default 32-bit microsecond counter
    wraps after about 71 minutes and makes long-running `/monitor` totals
    misleading.
- PlatformIO environment-specific `board_build.cmake_extra_args` must include
    parent environment arguments when overriding the field; otherwise ESP-IDF
    component flags such as `ENABLE_SPI` are silently dropped for that
    environment.
- The AI target uses `partitions/esp32_16mib_ai.csv`. Its full upload packs the
    selected ESP-SR models and flashes the resulting image to the dedicated
    `model` partition in addition to the normal bootloader, partition table,
    and app images.
- Power owns WiFi network selection. Its tracked `wifiConfig` retains both the
    station and access-point credential records, and `wifiConfig.mode` selects
    which one is active. Provision both passwords into the Power node's
    `wifi-sta-pass` and `wifi-ap-pass` secret keys before switching freely
    between modes; only the selected secret is read during WiFi startup.
- Top-level `CMakeLists.txt` requires `SRC_ROOT` and maps it to the selected
    source subtree
- `src/CMakeLists.txt` maps PlatformIO environments to source roots through
    each environment's `SRC_ROOT` CMake argument.

### SDKConfig

The `sdkconfig.<env>` files are generated from `sdkconfig.stack.*` templates. A
`build_script` assembles all sections with an `extends` in `platformio.ini` if
`sdkconfig.stack.<group>` exists. I.e. if `env:master` extends `esp32s3`, which
extends `esp32`, the build script will add to `sdkconfig.master.defaults` in
order:

- `sdkconfig.stack.esp32`
- `sdkconfig.stack.esp32s3`
- `sdkconfig.stack.master`

If options have changed, it deletes the generated `sdkconfig.<env>` file for
every environment using that defaults file to force regeneration.

Flash size entries must set both the Kconfig choice symbol and the string
value, for example `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y` plus
`CONFIG_ESPTOOLPY_FLASHSIZE="4MB"`. Setting only the string leaves ESP-IDF free
to regenerate the ignored `sdkconfig.<env>` file with its default 2 MiB choice.
The shared ESP32 stack provides the 4 MiB default; board stacks only override it
for larger flash variants such as ESP32-S3-N16R8.

Hardware UART console baud changes require `CONFIG_ESP_CONSOLE_UART_CUSTOM=y`;
`CONFIG_ESP_CONSOLE_UART_DEFAULT=y` keeps ESP-IDF's default 115200 baud even if
`CONFIG_ESP_CONSOLE_UART_BAUDRATE` is present in the defaults file. USB
Serial/JTAG targets keep the runtime command console on USB and leave UART0 as
the early boot and panic-output path. PlatformIO's host-side `monitor_speed`
must match the active console transport.

The command console is a small line editor over the same runtime console:
slash-prefixed commands can be edited with Backspace/DEL, Tab completes
registered commands and subcommands, Up/Down navigate the last five accepted
commands, and `/help` emits the commands registered on the current node,
including subcommands and arguments. Each input byte redraws the current command
buffer so log output does not permanently hide in-progress input. Console input
events wake the command task directly; the periodic task interval is only a slow
liveness fallback.

Managed `TaskController` tasks default to ESP-IDF static task creation. Their
TCB and stack storage are exact-size task-owner storage derived from
`StaticConfig/Stacks.hpp`, so repo-owned task stack RAM is visible in the
firmware image without reserving a worst-case shared pool. Individual managed
tasks can opt back into dynamic creation through
`TaskController::Config::allocation`; `StaticConfig::TaskStacks` also provides
constexpr storage-mode defaults so dynamic builds can avoid compiling in unused
static task storage without preprocessor switches. ESP-IDF, driver, and
library-owned tasks remain outside this project abstraction. Use
`bin/task-stacks <env>` to inspect configured task-stack RAM; when an ELF exists
it also reports matching firmware symbols. The logging aggregator ring buffer also
supports static or dynamic allocation; static storage is selected by the
constexpr `LoggingConfig::aggregatorRingBufferStatic` flag.

The v2 Master low-speed SPI bus uses GPIO1/GPIO2/GPIO3, so its former
GPIO36/GPIO37 PSRAM conflict no longer exists. Master and Media enable the
ESP32-S3 Zero's PSRAM and use USB Serial/JTAG for their consoles, leaving
GPIO43/GPIO44 available to the board signals.

The ESP32-S3 master has also shown a practical MISO sample-point quirk during
SPI bring-up: slower clocks can produce a stable one-bit-late receive stream,
while the same wiring works at the empirically selected higher clock. Treat SPI
clock reductions as a signal-integrity/timing change that must be revalidated
on hardware, not as automatically safer.

## Verification Model

There is currently no unit-test or host-side simulation harness.

`include/Setups/PubSubTest.hpp` provides an on-device PubSub integration harness
that tracks expected recipients, message-pool release, and transport egress release
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

The local shared-bus simulation treats peer readiness as a bus-level event: when
a peer becomes available, the router wakes its owning PubSub node so
subscriptions can be replayed to peers that missed earlier control-plane
advertisements. Router fanout is synchronous in the local shared-bus transport:
enqueueing a frame clocks it into target peer queues immediately instead of
staging an extra router egress dispatch. Shared-bus edge and router transports
now treat the local link queues as the durable simulator handoff point and no
longer retain a second immediate-release egress arena copy. This keeps the
single-device simulator closer to the intended low-utilization SPI bus and
avoids measuring artificial queue, lock, and arena overhead as bus latency.
Transport ingress is now polled through one receive-and-publish pass instead of
a separate node receive pass followed by drainer polling. Frames received from
transport ingress now retain their serialized wire image after a fixed-header
peek, so forwarding transports can reuse the original bytes instead of
reserializing the same envelope on every hop. Full frame CRC validation is
deferred until a local payload read actually occurs, which keeps forward-only
traffic closer to cut-through behavior while preserving end-to-end integrity
checks for consumers. Transport ingress now also has a selective direct-relay
fast path: if a received frame is neither control-plane nor locally subscribed,
and exactly one downstream transport is interested, the node can hand the raw
wire bytes straight to that downstream transport without first retaining the
frame in the local ingress buffer. If the direct handoff backpressures or
routing is more complex, the existing buffered ingress path remains the
fallback.

PubSub message owner pools are bounded hot-path structures. They use short
critical sections around slot bookkeeping instead of blocking task mutexes, and
payload encoding snapshots the stored value before serializing it outside the
lock. This keeps transport completion and owner-release paths independent of
task-priority scheduling while preserving deterministic pool capacity.

PubSub task configurations use notify wakeups so local publishes and transport
ingress can run before the next periodic poll. Catch-up polling is disabled for
PubSub tasks because missed periodic ticks should not create extra zero-delay
work when the task is already being driven by explicit notifications. The
single-device simulator harness leaves PubSub node task core affinity free;
pinning the simulated topology to one CPU has caused boot-time queue backlog and
task failures.

See [pubsub.md](pubsub.md) for the current PubSub architecture and development
guidance.

Task monitoring treats managed task-controller runners and platform/system tasks
as distinct sources. Managed native task handles are tracked explicitly so the
system task source skips those tasks instead of reporting duplicates.
Task-controller auto-restart reuses the existing managed runner entry and its
metric handles; restart counts therefore remain attached to the same metric
group instead of registering a new group for every failed task instance.
Task-controller lifecycle begin/end is controller-scoped and has no task config;
each managed runner receives and retains its own `TaskController::Config` when
started.

Meaningful verification currently means:

- the relevant build passes
- the changed code path is reviewed in context
- the result looks correct at first glance for the intended embedded use

For master WiFi/socket work, `bin/net-probe` provides host-side UDP/TCP
send/receive helpers and route lookup. It is a diagnostic utility, not a
replacement for future unit tests or a `PLATFORM_HOST` build.

Service facades are intended to stay lightweight. In particular, `Services/*`
headers should not pull full backend implementations into unrelated subsystems.

Logging egress over UART now uses the ESP-IDF TX software buffer configured via
`include/StaticConfig/Console.hpp`, and the logging sink no longer forces a full
UART drain after every record by default. This keeps logger callers decoupled
through the aggregator ring buffer while letting the UART driver absorb bursty
output without stalling the emitter task on each line.
USB Serial/JTAG console builds skip console-sink formatting and writes while no
USB host is attached. This is based on ESP-IDF's USB Serial/JTAG connection
status, which detects host SOF packets rather than whether a terminal program
has opened the serial port. Hardware UART console builds remain always writable
because the UART has no reliable connection signal.
ESP-IDF native logs are routed into the same aggregator after `CoreSetup`
installs the logging backend, through `LoggingBackend/detail/NativeLogBridge`.
Early or constrained ESP-ROM logs stay on ESP-IDF's original output path so the
null logger cannot recursively call back into the native log hook.
The bridge also lowers ESP-IDF's runtime default level to the configured `<esp>`
startup threshold when needed, so below-threshold native records are rejected
before ESP-IDF calls the vprintf sink.

If stronger verification is required for a task, that should be requested
explicitly.

## Documentation Boundary

- Prefer MCP/LSP for up-to-date symbol, file, and call-structure discovery
- Use `docs/` for project intent, active scope, workflows, and decisions that
    are not reliably inferable from code alone
