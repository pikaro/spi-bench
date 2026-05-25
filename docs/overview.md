# Project Overview

This repository contains an embedded C++23 library and application stack for
ESP32-class microcontrollers, built with PlatformIO on top of ESP-IDF.

The intended end state is a festival "rave stick": a reliable multi-node LED
controller that can run unattended for many hours. A bus master handles WiFi and
central coordination; a media node processes I2S audio and publishes FFT / beat
events; four ESP32-S3 GPU nodes render LED frame segments; side nodes over
RS485, I2C, and BLE provide sensors, bulbs, and future peripherals.

The system is event-driven because full LED frame data is too expensive to
publish at the target 125 fps. Nodes publish compact events such as FFT frames,
beats, bell strikes, warnings, errors, and metrics. GPU nodes subscribe to those
events and render their own LED segment locally.

## Current Scope

- `env:master`, `env:media`, `env:gpu0`, `env:gpu1`, and `env:io` are active
    targets for the current hardware PubSub network. The master owns a
    low-speed SPI bus currently wired to media, a high-speed SPI bus shared by
    GPU0 and GPU1, and one RS485 link to IO.
- Production node entrypoints use `include/Setups/PubSubNetwork.hpp`, which
    registers the real transports and exposes `PubSubService` without starting
    synthetic test publishers or subscribers. The synthetic multi-board
    regression harness remains in `include/Setups/PubSubStarTest.hpp`.
- The master firmware has optional WiFi enabled through the `Wifi` component.
    Current operation is either station mode or access-point mode, selected by
    ignored local master credentials/config; combined AP+STA mode is not part
    of the active runtime shape.
- Hardware SPI now supports multiple logical master links sharing the same
    ESP32 SPI bus. The active high-speed bus uses one PubSub SPI router
    transport with GPU0 and GPU1 peers. The low-speed bus remains a
    point-to-point media link for this prototype stage; the target topology
    still needs four GPU peers on high-speed SPI and media, LoRA radio, and GPS
    on the low-speed bus.
- GPU LED presentation is currently synchronized by a master-driven GPIO
    present strobe: master GPIO6 produces a 125 Hz rising-edge cadence and GPU
    nodes receive it on GPIO10. GPU LED ownership is explicit per firmware
    image so interleaved wiring can map `gpu0` to groups 0 and 2 and `gpu1` to
    groups 1 and 3.
- There is no `env:slave` in this checkout. Historical RS485 slave
    documentation may refer to that environment, but `platformio.ini` no
    longer defines it.
- When a task touches shared wire, PubSub, Clock, or platform abstractions,
    build `master`, `media`, `gpu0`, `gpu1`, and `io` unless the task is
    explicitly scoped to one environment.

## Design Intent

- Platform-agnostic embedded abstractions where practical
- Header-heavy organization with small focused components
- Preference for deterministic behavior and low runtime overhead
- No desktop test harness at present; correctness is currently validated by
    successful compilation and careful code review in context

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
- `include/Wifi/`: ESP32 WiFi lifecycle wrapper with bounded project-owned
    config, status reporting, and master command integration
- `include/Network/`: thin lwIP UDP/TCP socket wrappers and diagnostic commands
    for one-shot network probes
- `include/Bluetooth/`: fixed-capacity BLE central abstraction with an ESP-IDF
    NimBLE backend for node-local device profiles
- `include/Wheel/`: BLE wheel device-profile driver and wire payload used by
    `io` to publish wheel rotation events
- `include/Audio/`: media-node compile-time selected I2S, LittleFS WAV,
    Bluedroid A2DP, or BTstack A2DP audio input, FFT analysis, magnitude
    scaling, and beat detection; see
    [audio.md](audio.md)
- `include/LedTopology/` and `include/LedDisplay/`: GPU-node logical LED
    topology, compile-time ownership, FastLED-backed output, local animation
    playback, primitive drawing helpers, and PubSub animation commands; see
    [animation-pipeline.md](animation-pipeline.md)
- `include/Generated/Wire/`: generated wire-format support code
- `include/Wire/Rs485/`: point-to-point RS485 wire layer; see
    [wire-rs485.md](wire-rs485.md)
- `include/Wire/I2C/`: ESP32 I2C master bus, fixed-capacity device registry,
    and low-speed peripheral drivers; see [wire-i2c.md](wire-i2c.md)
- `include/Wire/Spi/`: in-progress DMA-oriented SPI wire layer with a
    component-owned ESP32 platform abstraction; see
    [spi-transport-plan.md](spi-transport-plan.md)
- `src/master/`, `src/media/`, `src/gpu/`, and `src/io/`:
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
shows cyan while booting, blue after core setup, and green after the node target
setup completes. Consumers register named states through the tiny
`StatusLed::Directory` handle and flip the returned `StateHandle`; active
critical, error, warning, then informational states are selected in priority
order and cycled every 500 ms. The current no-FastLED backend drives one
WS2812B with ESP-IDF RMT and only writes the retained LED color when the
selected RGB value changes.

### IO Lighting

The current `env:io` board is an ESP32-C3 side node intended to sit inside the
treasure chest, separate from the main controller board. Its local `LedPwm`
setup exposes configured LED contexts for two external E27 bulbs and one
onboard gold LED. Public LED commands still require a returned `LedContext`, so
callers can only address LEDs present in the node configuration.
`PeripheralLedConfig::configured` marks populated static slots; unconfigured
slots are ignored so nodes can own fewer LEDs than `LedPwmConfig::maxLeds`
without creating dummy GPIO outputs. The media node uses the same `LedPwm`
component for the active-high GPIO26 FFT beat indicator.
The same node owns the ship's bell input as an active-high GPIO button with an
external pulldown. Button ISR events are queued and published locally through
PubSub before higher-level lighting code reacts to them; the button task also
polls configured GPIO levels and deduplicates transitions so missed ISR wakeups
do not leave a changed level invisible. Button metrics are split between
`btnCore` for ISR drops and publish failures, `buttons` for published events,
and `btnDiag` for ISR/poll/deduplication diagnostics.

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

`Pulse` is the first animation payload. Additional effects such as chest
twinkle, beat flicker, and bell/drop flares should be added as new `Animation`
variant payloads with small self-contained parameter structs, keeping command
objects queue-copyable and allocation-free.
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
- Master WiFi credentials are not tracked. Create
    `src/master/wifi_credentials.hpp` from
    `src/master/wifi_credentials.example.hpp` for local station/AP credentials;
    the firmware falls back to disabled WiFi when that ignored file is absent.
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

The current master board wiring uses GPIO36/GPIO37 for the low-speed SPI bus.
Those pins overlap the ESP32-S3 OPI PSRAM signal set on the devkit-style board,
so `env:master` deliberately disables PSRAM through `sdkconfig.stack.master`.
Changing this requires changing the board wiring or the SPI pin configuration;
otherwise the SPI driver can wedge external memory access and trigger a system
watchdog reset instead of a normal panic.

The ESP32-S3 master has also shown a practical MISO sample-point quirk during
SPI bring-up: slower clocks can produce a stable one-bit-late receive stream,
while the same wiring works at the empirically selected higher clock. Treat SPI
clock reductions as a signal-integrity/timing change that must be revalidated
on hardware, not as automatically safer.

## Verification Model

There is currently no unit-test or host-side simulation harness.

`include/Setups/PubSubTest.hpp` provides an on-device PubSub integration harness that
tracks expected recipients, message-pool release, and transport egress release
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
