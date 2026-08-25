# INA2xx Driver Implementation Plan

Status: **complete**
Last updated: **2026-08-26**

## Goal

Add an allocation-free, header-only `Wire::I2C::Ina2xx` component following the
existing I2C device patterns. The first implementation supports INA219 and
INA226 selected explicitly at construction time. Its public API is shaped so a
different INA228 register backend can be added later without pretending that
the INA228 is register-compatible.

The driver periodically samples measurements, publishes 32-bit metrics, checks
bus-voltage and signed-current operating windows, and exposes only the alert
features supported by the selected model.

## Bus/current window revision — 2026-08-26

The original implementation interpreted the requested voltage window as raw
shunt drop. Hardware bring-up clarified that the intended voltage is the
single-ended VBUS measurement. This revision supersedes that part of the
original design while preserving shunt voltage as sampled diagnostic data.

- Bus-voltage limits use unsigned millivolts, matching the existing bus metric
  and the useful resolution of INA219/INA226 without fake microvolt precision.
- Signed current limits use microamps, matching the calibrated sample and
  retaining reverse-current support.
- Both windows independently produce the existing five-state practical/absolute
  warning and error classification. Limit events identify which measurement
  changed state.
- Equal adjacent limits are valid, allowing a zero lower bus boundary without
  inventing an unmeasurable negative VBUS threshold.
- INA219 gain selection is derived from expected maximum current and shunt
  resistance rather than from an application alert window.
- INA226 hardware ALERT selection becomes semantic: bus over/under thresholds
  map directly to VBUS comparator values; current over/under thresholds are
  converted to shunt voltage using the configured shunt resistance.
- INA219 continues to reject every hardware-alert configuration. INA226 still
  accepts only one hardware comparator function at a time.

## Confirmed requirements and design decisions

- The caller selects `Ina2xxModel::Ina219` or `Ina2xxModel::Ina226` when the
  driver is constructed. There is no unreliable automatic probing fallback.
- INA228 is deliberately out of scope for this change. The common layer deals
  in semantic samples and capabilities; model-specific code owns register
  widths and encodings so INA228 can later use 16-, 24-, and 40-bit registers.
- Sampling is cooperative through `work(nowMs)` at a configurable interval,
  consistent with the repository's other periodically serviced components.
- All exported metric storage is 32-bit. Signed measurements use the existing
  signed-gauge metric support. Wider integer arithmetic may be used only for
  checked intermediate scaling calculations; no 64-bit accumulators or
  metrics are introduced.
- The configured voltage limits apply to VBUS in millivolts. A second signed
  window applies to calibrated current in microamps. Raw shunt voltage is
  sampled for diagnostics and comparator conversion, not configured as the
  application voltage window.
- Practical under/over transitions are warnings. Absolute under/over
  transitions are errors. Notifications are edge-triggered on state changes,
  rather than repeated on every sample.
- The periodic software window check supports both directions on both models.
  It is distinct from the INA226 ALERT pin: INA226 has only one alert-limit
  register and therefore cannot provide simultaneous hardware under- and
  over-threshold alerts.
- A hardware-alert callback can be registered only for INA226. Attempting to
  register it on INA219 returns `CoreError::NotSupported`, as required.
- No I2C access occurs in a GPIO ISR. The ISR records pending work; `work()`
  reads and clears the INA226 alert state and invokes the callback in task
  context.
- Current and power calibration is explicit. Configuration includes shunt
  resistance and expected maximum current; invalid or unrepresentable
  calibration is rejected during `begin()`.
- A failed sample never overwrites the last valid sample. Metrics expose sample
  failures and age/staleness so zero is not confused with fresh zero current.

## Public API shape

The exact spelling may be adjusted to match adjacent conventions while the
semantics remain fixed:

```cpp
enum class Ina2xxModel { Ina219, Ina226 };

enum class Ina2xxLimitState {
  AbsoluteUnder,
  PracticalUnder,
  Normal,
  PracticalOver,
  AbsoluteOver,
};

struct Ina2xxBusVoltageWindow {
  std::uint32_t absoluteMinMillivolts;
  std::uint32_t practicalMinMillivolts;
  std::uint32_t practicalMaxMillivolts;
  std::uint32_t absoluteMaxMillivolts;
};

struct Ina2xxCurrentWindow {
  std::int32_t absoluteMinMicroamps;
  std::int32_t practicalMinMicroamps;
  std::int32_t practicalMaxMicroamps;
  std::int32_t absoluteMaxMicroamps;
};

struct Ina2xxConfig {
  DeviceConfig device;
  std::uint32_t sampleIntervalMs;
  std::uint32_t shuntMicroOhms;
  std::uint32_t expectedMaxCurrentMicroamps;
  Ina2xxBusVoltageWindow busVoltage;
  Ina2xxCurrentWindow current;
  std::optional<Ina2xxHardwareAlertConfig> hardwareAlert;
  const char* metricsGroupName;
};

struct Ina2xxSample {
  std::int32_t shuntMicrovolts;
  std::uint32_t busMillivolts;
  std::int32_t currentMicroamps;
  std::int32_t powerMilliwatts;
  // Future INA228 semantic values remain zero on INA219/INA226.
  std::int32_t temperatureMillicelsius;
  std::uint32_t energyMillijoules;
  std::int32_t chargeMillicoulombs;
  std::uint32_t capturedAtMs;
  std::uint32_t validCapabilities;
};
```

The limit callback receives the affected bus/current measurement, new limit
state, and sample. The INA226 hardware callback receives the decoded semantic
bus/current alert function and sample context. Both callbacks use the
repository's allocation-free callback conventions.

## Capabilities

| Capability | INA219 | INA226 | Future INA228 |
| --- | --- | --- | --- |
| Shunt/bus/current/power sample | yes | yes | yes |
| Periodic bus-voltage/current windows | yes | yes | yes |
| Hardware threshold ALERT callback | no | yes, one selected function | yes, backend-specific |
| Manufacturer/die identity check | no | yes | yes |
| Temperature | no | no | planned |
| Energy/charge accumulators | no | no | planned, without forcing 64-bit metrics |

Unsupported measurements keep their metric values at zero, but a validity or
capability metric distinguishes unsupported data from a real zero reading.

## Implementation phases

### Phase 1 — Baseline and interface

- [x] Read `docs/overview.md`, `docs/commands.md`, and the existing I2C driver
  documentation and patterns.
- [x] Verify INA219 and INA226 register behavior against official TI
  datasheets.
- [x] Confirm that signed-gauge support already exists in the current working
  tree; preserve that user-owned change and build against it.
- [x] Create this maintained plan before implementation.
- [x] Add model, capability, configuration, sample, limit-event, and
  alert-event interface types.
- [x] Add the public `Wire::I2C::Ina2xx` facade export.

### Phase 2 — Register backends and lifecycle

- [x] Implement common big-endian 16-bit register I/O helpers over
  `Wire::I2C::Device`.
- [x] Implement INA219 configuration, conversion decoding, overflow detection,
  and calibration encoding.
- [x] Implement INA226 configuration, conversion decoding, overflow detection,
  identity verification, calibration, mask/enable, and alert-limit encoding.
- [x] Validate addresses, intervals, ordered voltage limits, device ranges,
  shunt resistance, calibration range, and model-specific options at `begin()`.
- [x] Keep model dispatch at lifecycle/sample boundaries rather than spreading
  register conditionals through the public API.

### Phase 3 — Sampling, limits, alerts, and metrics

- [x] Implement rollover-safe interval scheduling in `work(nowMs)` and an
  explicit immediate-sample operation for diagnostics/tests.
- [x] Convert raw values to checked 32-bit engineering units and retain the
  last valid `Ina2xxSample`.
- [x] Classify each bus-voltage and signed-current sample into independent
  five-state operating windows.
- [x] Invoke callbacks and warning/error logging only on state transitions;
  report absolute excursions through the normal error path.
- [x] Implement INA226 ALERT GPIO deferral and decoded task-context callback.
- [x] Reject hardware-alert registration/configuration for INA219 with
  `CoreError::NotSupported`.
- [x] Register a per-instance metrics group containing the available 32-bit
  gauges plus sample/failure/overflow/alert counters, state, validity, and age.
- [x] Leave future-only temperature/energy/charge gauges at zero and expose a
  capability/validity mask.

### Phase 4 — Tests and verification

The repository currently has no unit-test or host-simulation harness. Adding a
fake `Master` transport would expand this task into a shared I2C refactor, so
pure register conversions and limit classification are checked with
compile-time assertions; integration is checked by compiling the existing
facade consumers.

- [x] Add compile-time checks for signed conversion, register scaling,
  calibration construction, threshold encoding, and all voltage-window states.
- [x] Check the compile-time capability matrix, especially INA219 hardware-
  alert rejection.
- [x] Check that the INA226 config represents exactly one hardware threshold
  direction and that its status bits do not overlap.
- [x] Build the relevant firmware environments with `bin/build`; verify the
  shared metrics changes with `io` and compile the I2C facade through `media`.
- [x] Inspect SARIF output for compile diagnostics and fix only issues caused by
  this component.

### Phase 5 — Documentation and handoff

- [x] Add an INA2xx section to `docs/wire-i2c.md` with construction, config,
  sampling, units, voltage-state behavior, alert restrictions, and an example.
- [x] Update `docs/overview.md` only where needed to expose the new component.
- [x] Document the INA228 extension boundary and the intentionally unsupported
  features without claiming implementation.
- [x] Record completed verification and any known hardware-test gap below.
- [x] Mark this plan complete.

### Phase 6 — Scratch-node INA226 presence test

- [x] Configure scratch I2C bus 0 at 100 kHz with SDA on GPIO10 and SCL on
  GPIO20, enabling the ESP32's internal pull-ups for bring-up tolerance.
- [x] Instantiate the model-selected driver as INA226 at address `0x40`.
- [x] Run `begin()` after core setup so startup verifies the INA226 TI
  manufacturer and die IDs and fails visibly when the device is absent or is
  the wrong model.
- [x] Build and flash `env:scratch`, then capture the successful presence log.
- [x] Record the hardware result and mark this plan complete again.

### Phase 7 — Separate bus-voltage and current windows

- [x] Reopen this maintained plan and record the corrected semantics before
  implementation.
- [x] Replace the raw-shunt limit config with bus-voltage and signed-current
  windows and add the measurement source to limit events.
- [x] Derive INA219 gain and INA226 current-alert encoding from current and
  shunt resistance.
- [x] Add INA226 bus over/under hardware-alert support while preserving
  model-feature validation.
- [x] Track and publish independent bus/current limit states.
- [x] Update the scratch configuration and all INA2xx documentation/examples.
- [x] Build and inspect `scratch`, flash the attached node, and verify live
  samples and independent limit states.
- [x] Record verification and mark this plan complete.

## Planned file scope

- `include/Wire/I2C/Interfaces/Ina2xx*.hpp` — public model/config/event/sample
  types (the final split will follow local interface-file granularity).
- `include/Wire/I2C/detail/Ina2xx.hpp` — lifecycle, scheduling, semantic API,
  state transitions, and model dispatch.
- `include/Wire/I2C/detail/ina2xx/` — private INA219/INA226 register details and
  conversion/calibration helpers.
- `include/Wire/I2C/Facade.hpp` — public export.
- compile-time assertions beside the pure private conversion helpers — focused
  checks without adding a host-test framework.
- `docs/wire-i2c.md`, `docs/overview.md`, and this plan — documentation.

The driver implementation itself remains application-neutral. The explicit
GPIO10/GPIO20, address-`0x40` integration requested for hardware bring-up lives
only in `env:scratch`.

## Verification record

- `bin/build -e io`: **passed** on 2026-08-25. This exercises the current
  signed-gauge metrics backend and completed firmware link/image generation.
- `bin/build -e media`: the media translation unit includes
  `Wire/I2C/Facade.hpp` and SARIF contains **no INA2xx diagnostics**. The full
  environment remains blocked by pre-existing ESP32S3 pin/bootloader target
  mismatches and AudioTools errors outside this component.
- `bin/build -e master`: blocked before relevant compilation by the existing
  missing `lwip/sockets.h` include in the master network platform header.
- Scoped `git diff --check`: **passed**.
- Pure conversion, calibration, range-selection, limit-state, capability, and
  mask-bit invariants compile as `static_assert` checks.
- `bin/build -e scratch`: **passed** on 2026-08-26; the scratch translation unit
  and linked firmware have no SARIF diagnostics.
- `bin/build -e scratch -t upload`: **passed** on 2026-08-26 and flashed the
  configured ESP32-C3 SuperMini successfully.
- The physical INA226 at address `0x40` passed its manufacturer/die identity
  check over bus 0 at 100 kHz on SDA GPIO10/SCL GPIO20. The captured startup
  log reported `INA226 detected at I2C address 0x40`.
- The bus/current-window firmware was rebuilt and flashed on 2026-08-26.
  Repeated live metric snapshots reported approximately `4977 mV`, `101278 uA`,
  `504 mW`, `busState=Normal`, and `curState=Normal`, with zero sample failures
  and math overflows. The `202 uV` shunt reading and configured `2 mOhm` shunt
  independently imply approximately `101 mA`, matching the calibrated current.
- Live signed-gauge output exposed that four widened 11-character fields no
  longer fit the 128-byte metrics log buffer. The formatter now derives three
  fields per line from the buffer and field widths; repeated `/metrics` dumps
  complete without a stack-protector reset.
- Live current/shunt comparison exposed an INA226 calibration value with its
  reserved bit 15 set. Calibration now raises `Current_LSB` when required so
  `FS[14:0]` remains representable; compile-time checks cover the attached
  2-mOhm/2-A configuration.
- Hardware ALERT pin assertion/polarity and physical INA219 measurements remain
  hardware-test gaps because neither was wired for this bring-up.
