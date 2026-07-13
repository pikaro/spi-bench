# Status LED Implementation Plan

## Current Understanding

The project needs a small global `StatusLed` service that can be used by any
component, like `LoggingService` and `MetricsService`. It should initialize the
physical LED early, show boot progress colors, then enter normal operation where
the highest active severity wins:

- `Critical`: cycle active critical states only
- `Error`: cycle active error states only
- `Warning`: cycle active warning states only
- `Informational`: show the current informational state

Active states should cycle at a 500 ms interval. Consumers register named states
with a unique color and a severity kind, then hold a tiny handle that can set or
reset the condition without queueing work. The LED should only be written when
the displayed color changes because WS2812B LEDs retain their last color.

## Assumptions

Blocking assumptions before implementation:

- Strictly using LEDC for WS2812B output would be non-functional. LEDC can drive
  ordinary PWM LEDs, but a WS2812B needs a timed one-wire bitstream. The project
  already includes `esp_driver_rmt` in `CMakeLists.txt`, so the no-FastLED
  backend should be a tiny ESP-IDF RMT WS2812B backend. If LEDC is mandatory for
  non-GPU nodes, then the component can only support a single-channel PWM status
  LED there, not a color WS2812B status LED.

Non-blocking assumptions used for this design:

- Registration happens during component/node setup, not from hot loops.
- Runtime condition flips happen from task context, not from ISR context.
- A fixed maximum number of states is acceptable and should live in
  `include/StaticConfig/StatusLed.hpp`.
- Color uniqueness is enforced per node-local directory, which matches the
  node-local nature of the service.
- The initial global states are registered by `StatusLed` itself so they use the
  same directory/handle path as consumer states.

## Existing Patterns To Reuse

- Global service binding:
  [include/Services/Logging.hpp](/Users/david.reis/src/dre/uc/spi/include/Services/Logging.hpp)
  and
  [include/Services/Metrics.hpp](/Users/david.reis/src/dre/uc/spi/include/Services/Metrics.hpp)
  use small service facades with static backend pointers.
- Core service startup:
  [include/Setups/Core.hpp](/Users/david.reis/src/dre/uc/spi/include/Setups/Core.hpp)
  owns logging, metrics, command, filesystem, and monitoring objects and binds
  global services during setup.
- Component layout:
  `LedPwm` uses `Facade.hpp`, `Interfaces/`, `detail/`, platform selection, and
  static config:
  [include/LedPwm/detail/LedPwm.hpp](/Users/david.reis/src/dre/uc/spi/include/LedPwm/detail/LedPwm.hpp)
  and
  [include/LedPwm/Interfaces/Config.hpp](/Users/david.reis/src/dre/uc/spi/include/LedPwm/Interfaces/Config.hpp).
- Notify-driven tasks:
  PubSub and several wire components wake managed tasks through
  `TaskController::Controller::signalTaskDirect`, matching the desired
  low-latency condition flip path.
- FastLED dependency isolation:
  `LedDisplay` keeps `<FastLED.h>` inside output/renderer backends:
  [include/LedDisplay/Outputs/FastLedOutput.hpp](/Users/david.reis/src/dre/uc/spi/include/LedDisplay/Outputs/FastLedOutput.hpp).
- Fixed directories:
  [include/Generic/Directory.hpp](/Users/david.reis/src/dre/uc/spi/include/Generic/Directory.hpp)
  provides the project pattern for registration-time directories. `StatusLed`
  should reuse the concept but keep active-state lookup as bitmasks so condition
  flips avoid a mutex and heap work.
- Hardware pin hints:
  S3 devkit and S3 zero already expose `Pin::StatusLed` in
  [HardwareESP32S3Devkit.hpp](/Users/david.reis/src/dre/uc/spi/include/Platform/platform/PlatformESP32/hardware/HardwareESP32S3Devkit.hpp)
  and
  [HardwareESP32S3Zero.hpp](/Users/david.reis/src/dre/uc/spi/include/Platform/platform/PlatformESP32/hardware/HardwareESP32S3Zero.hpp).
  Media currently notes `statusled GPIO16` in
  [src/media/config.hpp](/Users/david.reis/src/dre/uc/spi/src/media/config.hpp).

## Implemented Files

- `include/Services/StatusLed.hpp`
- `include/StatusLed/Facade.hpp`
- `include/StatusLed/Interfaces/Config.hpp`
- `include/StatusLed/Interfaces/Types.hpp`
- `include/StatusLed/detail/StatusLed.hpp`
- `include/StatusLed/detail/Types.hpp`
- `include/StatusLed/detail/PlatformSelect.hpp`
- `include/StatusLed/detail/platform/RmtWs2812.hpp`
- `include/StaticConfig/StatusLed.hpp`

`Directory` and `StateHandle` live in `Interfaces/Types.hpp` as tiny value
handles. There is no separate directory implementation file because the runtime
storage is owned directly by `detail/StatusLed.hpp`.

## Public API Shape

```cpp
namespace Totem::StatusLed {

enum class StateKind : uint8_t {
    Informational,
    Warning,
    Error,
    Critical,
};

struct RgbColor {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

struct StateDef {
    const char *name;
    RgbColor color;
    StateKind kind;
};

class StateHandle {
  public:
    ReturnCode set(bool active) const;
    ReturnCode reset() const;
};

class Directory {
  public:
    std::expected<StateHandle, ReturnCode> registerState(StateDef def);
    ReturnCode set(StateHandle handle, bool active);
};

} // namespace Totem::StatusLed
```

`StateHandle` should remain pointer/index sized: a backend pointer plus an
8-bit state id is enough. Runtime `set()` should only update atomic bitmasks and
mark the service dirty for the next `CoreSetup::work()` pass.

## Runtime Data Model

`StatusLed` owns:

- fixed `std::array<StateSlot, StatusLedConfig::maxStates>`
- active masks per severity, stored as `std::atomic<uint32_t>`
- last displayed state index and color
- dirty flag for state changes that should be evaluated on the next work pass
- output backend object

Do not start with a dedicated `TaskController` task. The memory notes for this
project call out RAM pressure around static task stacks, especially on Media.
`CoreSetup::work()` already runs from every node's main loop, so the first
implementation should add `statusLed.work(nowMs)` there. That gives a bounded
1 ms-ish response with no queue and no additional task stack. If later hardware
behavior requires task-context isolation, a notify-driven task can be added as a
separate change with an explicit RAM tradeoff.

Registration:

1. Validate name, kind, color, enabled output config.
2. Reject duplicate colors inside the local directory.
3. Store state in the first free static slot.
4. Return a tiny `StateHandle`.

Activation:

1. Atomically set or clear the bit for the state id.
2. Mark the service dirty.
3. Return without queueing or scanning the directory.

Display updates:

- `work(nowMs)` returns immediately if no dirty state exists and the 500 ms
  cycle deadline has not elapsed.
- On dirty/cycle work, it snapshots the active masks, selects the winning state,
  and writes the LED only if the selected RGB differs from the last write.
- Boot and abort paths may call a direct `showImmediate()` helper because they
  need visible state before the normal main loop runs or before `abort()`.

Display selection:

1. Find the highest severity with a nonzero active mask.
2. If no warning/error/critical is active, display the current informational
   state.
3. If multiple states at that severity are active, advance to the next set bit
   every `StatusLedConfig::cycleIntervalMs` (500 ms).
4. Call output only if RGB changed.

## Boot State Flow

The predefined informational states are:

- `Booting`: cyan
- `CoreReady`: blue
- `TargetsReady`: green

The predefined test states are:

- `LogError`: error, red
- `Abort`: critical, white or bright magenta

Startup should be:

1. Bind `StatusLedService` from the `CoreSetup` constructor, matching
   `MetricsBinding`.
2. Add `CoreSetup::beginStatusLedEarly(config)` and call it before the existing
   3000 ms delay in each node root. This is the only way to satisfy "as soon as
   possible" with the current roots in `src/master/main.cpp`,
   `src/media/main.cpp`, `src/gpu/main.cpp`, and `src/io/main.cpp`.
3. `beginStatusLedEarly()` initializes the output, registers global predefined
   states, binds the service, and shows `Booting` cyan.
4. At the end of `CoreSetup::setup()`, set `CoreReady` blue.
5. At the end of each node-specific `setup()`, set `TargetsReady` green.
6. Normal operation uses the active-state selection rules above from
   `CoreSetup::work()`.

## Output Backends

### RMT WS2812B

Use ESP-IDF RMT directly for non-FastLED builds. This avoids adding FastLED to
Media, IO, and Master and uses `esp_driver_rmt`, which is already in the base
component list.

The backend should:

- configure one TX channel for one LED
- encode one GRB pixel plus reset timing as a fixed 25-symbol payload
- store the last color and skip duplicate writes
- provide `begin(Pin, ColorOrder)`, `show(RgbColor)`, and `deinit()`

Implementation detail from the installed ESP-IDF RMT tests:

- use a 10 MHz RMT resolution so one tick is 0.1 us
- WS2812 bit 0: high 0.3 us, low 0.9 us
- WS2812 bit 1: high 0.9 us, low 0.3 us
- reset: low for at least 50 us

For one status LED, do not use the full example led-strip encoder. Build a
small stack/local `std::array<rmt_symbol_word_t, 25>` on every color change,
use `rmt_new_copy_encoder()`, call `rmt_transmit()`, and then
`rmt_tx_wait_all_done()` before returning so the payload lifetime is simple.
That costs less than a millisecond only when the displayed color changes and has
no per-cycle work while the LED state is stable.

### FastLED WS2812B

FastLED support remains a future backend option. The initial implementation
uses the RMT backend on all configured RGB status LEDs so the public component
and service headers do not include FastLED and non-GPU environments do not gain
that dependency.

### LEDC PWM

LEDC can be added as a separate single-color backend if a node intentionally
wants a plain PWM status LED. It cannot represent the required unique RGB state
colors on WS2812B hardware, so it is not the default backend for this feature.

## Logging And Abort Integration

Error test condition:

- Prefer recording this in `LoggingService::vlogfSite()` when
  `level == LogLevel::Error` and formatting succeeds. That catches `_log_e`,
  `LOG_LOC`, and runtime error logs once per actual record.
- `Services/StatusLed.hpp` must be lightweight and not include the `StatusLed`
  implementation, so `Services/Logging.hpp` can call it without an include
  cycle.

Critical test condition:

- Add `StatusLedService::recordCritical()` to `INTERNAL_ABORT_DELAYED` in
  [include/Macros/internal/Fail.hpp](/Users/david.reis/src/dre/uc/spi/include/Macros/internal/Fail.hpp)
  before the existing 100 ms delay and `abort()`.
- This is temporary test behavior, not the final critical-state model.

## Candidate Warning Conditions

These are initial handles to register, not necessarily all to implement in the
first code pass:

| Node | State | Kind | Color | Owner |
| ---- | ----- | ---- | ----- | ----- |
| All | PubSubDelayHigh | Warning | amber | PubSub test/monitoring |
| All | TaskRestarted | Warning | purple | Task registry/controller |
| Master | SpiPeerLate | Warning | orange | SPI star setup |
| Master | Rs485PeerLate | Warning | teal | RS485/star setup |
| Media | FftFrameLate | Warning | yellow | `AudioFft::FftAnalyzer` |
| Media | AudioSourceOffline | Warning | pink | active audio source |
| GPU | LedFrameLate | Warning | violet | `LedDisplay` |
| GPU | FftFrameStale | Warning | lime | `LedDisplay` FFT input |
| IO | ButtonIsrDrop | Warning | gold | `Buttons` |
| IO | Rs485Backpressure | Warning | cyan-green | RS485 slave |

Actual integration should start with low-risk examples that already have
metrics or clear failure counters, then add more node-specific states later.

## Verification Plan

Build the active environments after implementation:

```sh
pio run -e master
pio run -e media
pio run -e gpu0
pio run -e gpu1
pio run -e io
```

The project currently has no host-side unit test harness, so verification is
compile-time plus careful code review. Hardware validation should confirm:

- booting cyan appears before long setup work
- core-ready blue appears after `CoreSetup::setup()`
- target-ready green appears after node-specific setup
- one `_log_e` changes to the error color
- an abort path changes to critical before abort
- multiple active warnings/errors cycle at 500 ms
- duplicate color registration fails

## Implementation Status

- Done: `StatusLed` interfaces, static config, service facade, and null
  backend.
- Done: tiny directory handles and active-mask state engine.
- Done: direct ESP-IDF RMT WS2812B output backend.
- Done: `CoreSetup` binding, early node startup calls, core-ready and
  target-ready boot transitions.
- Done: temporary logging-error and abort-critical hooks.
- Done: documentation update in `docs/overview.md`.
- Deferred: optional FastLED backend. The RMT backend is sufficient for the
  configured one-pixel status LEDs and keeps dependencies isolated.
- Deferred: concrete node-local warning integrations. The component API now
  supports them; owners should register the warning handles in their own setup
  path when the warning shapes are finalized.

## Open Risks

- The exact earliest boot point needs care because current node roots delay
  three seconds before `core.setup()`.
- Media uses a local status LED config on `Pin::GPIO16` rather than adding a
  hardware alias.
- IO currently has only `LedPwm` onboard LED config in this checkout; if it has
  no RGB status LED, it should use an explicit disabled status LED config or a
  future single-color LEDC backend.
- RMT and FastLED can both consume ESP32 RMT resources. The status LED needs one
  channel; GPU display already uses FastLED/RMT for the main LED output.

## Iteration Log

- Draft 1: Captured requirements, local code patterns, the LEDC/WS2812B
  conflict, component layout, state model, and verification plan.
- Draft 2: Tightened early boot sequencing around the existing three-second
  node-root delays and replaced the RMT backend sketch with a concrete
  one-pixel copy-encoder plan from the installed ESP-IDF RMT API.
- Draft 3: Removed the initial dedicated task from the design to avoid another
  static stack on RAM-constrained nodes; normal cycling should run from
  `CoreSetup::work()` and state flips should only mark dirty atomic state.
- Draft 4: Reconciled the document with the implemented file layout, recorded
  that the first pass uses RMT on all configured RGB status LEDs, and marked
  concrete per-node warning integrations as deferred until their owners settle
  the final warning shapes.
