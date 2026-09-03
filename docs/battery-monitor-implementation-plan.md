# Battery Monitor and Pack Calibration Implementation Plan

Status: **software implemented; battery-dependent verification pending**
Last updated: **2026-08-26**

## Goal

Add a sensor-agnostic `BatteryMonitor` component that consumes successful
voltage/current/power samples, estimates remaining battery charge and energy,
and predicts time to depletion. It must work with a conservative built-in
battery model and optionally use a discharge profile learned from an
uninterrupted calibration run stored on LittleFS.

The INA2xx driver remains responsible only for measurement and sensor-specific
limits. Battery chemistry, state of charge, energy integration, profile
storage, and calibration state belong to the new component.

## Confirmed hardware and operating assumptions

- Battery chemistry is standard 4.2 V lithium-ion.
- The pack is 7S4P.
- Claimed pack capacity is 10 Ah, but it is explicitly untrusted and must be
  treated as a starting estimate until measured.
- Full pack voltage is nominally 29.4 V (`7 * 4.2 V`).
- The BMS cutoff voltage is unspecified. Calibration must observe and record
  the BMS cutoff; it must not attempt to infer or wait for unsafe cell
  breakdown.
- Charging is outside scope. Negative/charging current is not accumulated and
  invalidates an active discharge calibration when it exceeds measurement
  tolerance.
- The calibration load is a 50 ohm, 50 W resistor in a substantial copper
  chassis, suspended on metal clamps.
- At 29.4 V the resistor initially draws approximately 588 mA and dissipates
  approximately 17.3 W. Current and power fall as the pack discharges; the
  estimator integrates the measured values and does not assume constant
  current.
- The INA226 current reading has already been compared against a precision lab
  supply and closely agrees at the tested operating point.
- LittleFS is present and currently contains an empty filesystem intended for
  calibration profiles.

## Schematic-derived measurement domains

The focused schematic trace establishes these roles:

- Main battery monitor U8 is in series between battery input J5 and both buck
  input U7 and battery output J6. It therefore measures the complete
  battery-funded load, including the buck's input consumption and anything
  connected to J6.
- Logic monitor U9 is between the buck output and the 5 V source-selector
  switch. It measures the buck-powered logic rail and is informational plus an
  error detector; it is not a battery fuel gauge.
- During calibration, the source selector can power the logic from USB while
  the pack discharges through U8 and a load on J6. This keeps the logger and
  LittleFS alive when the BMS disconnects the pack.
- U8 and U9 share one I2C bus. Their module address straps must select different
  addresses, and the chosen U8/U9 address mapping must be documented in power
  node configuration.

Evidence: `schematic/perfboard-v2.py`, `power_distribution()`, lines 217-253,
cross-checked against a temporarily generated KiCad schematic and its analyzed
pin-to-net map.

## Architectural boundary

Data flows in one direction:

```text
INA2xx successful sample
          |
          v
BatteryMonitor::observe(BatteryMeasurement)
          |
          +-- charge/energy integration in RAM
          +-- SOC and time-to-empty estimation
          +-- calibration interval aggregation
          |
BatteryMonitor::work(nowMs)
          |
          +-- periodic LittleFS journal append/flush
          +-- profile finalization after observed BMS cutoff
```

### INA2xx responsibility

- Add an allocation-free `setSampleCallback()` matching the existing callback
  conventions.
- Invoke it exactly once after every successful sample, after the latest sample
  and metrics have been updated.
- Deliver practical and absolute limit states as successful samples so a
  low-voltage calibration point is not discarded or reported as an unhandled
  acquisition failure.
- Never invoke it for failed I2C reads or device math overflow.
- Document that the callback must be bounded and non-blocking. It must not
  perform filesystem I/O.
- Keep all battery-specific logic out of `Wire::I2C::Ina2xx`.

### Battery monitor responsibility

- Accept normalized semantic measurements rather than depending on INA2xx
  register details or model types.
- Perform fixed-point integration, estimation, calibration aggregation, state
  transitions, and persistence scheduling.
- Write LittleFS only from `BatteryMonitor::work()`, never from a sensor
  callback.
- Preserve the intended INA228 measurement/accumulator adapter boundary, while
  permitting unrelated current sensors without changing the estimator.

## Proposed public data model

Exact spelling may be adjusted to neighboring component conventions while
preserving these semantics:

```cpp
struct BatteryMeasurement {
    uint32_t capturedAtMs;
    uint32_t voltageMillivolts;
    int32_t currentMicroamps;
    int32_t powerMilliwatts;
};

enum class BatteryChemistry {
    LiIon4V2,
};

struct BatteryConfig {
    BatteryChemistry chemistry;
    uint8_t seriesCells;
    uint8_t parallelCells;
    uint32_t nominalPackCapacityMilliampHours;

    uint32_t sampleGapToleranceMs;
    uint32_t persistenceIntervalMs;
    uint32_t averagePowerWindowMs;

    // Per-cell values are scaled by seriesCells for pack limits.
    uint32_t practicalMinCellMillivolts;
    uint32_t practicalMaxCellMillivolts;
    uint32_t absoluteMinCellMillivolts;
    uint32_t absoluteMaxCellMillivolts;

    const char *profilePath;
    const char *metricsGroupName;
};
```

Defaults for this pack start with 7 series cells, 4 parallel cells, and a
nominal 10 Ah capacity. Chemistry defaults may supply 4.2 V full-cell behavior,
but every configured limit remains inspectable and validated. The practical
low-voltage default should be conservative; the observed BMS cutoff from a
completed calibration is metadata, not an excuse to weaken an application
warning threshold automatically.

Series count and chemistry determine voltage expectations. Capacity requires
either the nominal pack capacity or a completed calibration profile; series
count alone cannot determine remaining energy.

## Integration and estimation math

Use consecutive valid samples and rollover-safe elapsed time. Trapezoidal
averaging reduces quantization and load-step error:

```text
deltaCharge[mAh] = averageCurrent[mA] * deltaTime[s] / 3600
deltaEnergy[mWh] = averagePower[mW]   * deltaTime[s] / 3600
```

- Accumulate discharge charge and discharge energy independently.
- Store cumulative public values and metrics in 32-bit engineering units.
- Checked 64-bit values may be used only as bounded intermediate products and
  division remainders; do not introduce 64-bit metrics or unbounded public
  accumulators.
- Preserve fractional remainders between updates so sub-mAh and sub-mWh
  contributions are not repeatedly rounded away.
- Reject or mark uncertain intervals whose sample gap exceeds the configured
  tolerance.
- Ignore tiny signed-current noise around zero using a configured deadband.
- Treat meaningful negative current as charging/unsupported. It does not add
  capacity; during calibration it invalidates the run.

### Without a learned profile

- Use the configured nominal 10 Ah capacity as the provisional full-charge
  capacity.
- Use a deliberately simple idealized voltage-to-SOC estimate to initialize a
  monitor that starts part-full.
- Mark this initialization as low confidence because loaded Li-ion terminal
  voltage is not a unique SOC measurement.
- Once a full starting condition is established, use measured charge and
  energy integration as the primary estimate.

### With a learned profile

- Use the profile's measured usable charge and usable energy instead of the
  nominal claim.
- Treat the learned voltage curve explicitly as a **loaded** curve from the
  50 ohm calibration. Use it for low-confidence initialization only when the
  observed discharge current is reasonably close to the curve's representative
  current; otherwise fall back to the conservative idealized voltage estimate.
- Do not use the loaded curve as an open-circuit-voltage drift correction after
  relaxation. That would systematically bias SOC because the calibration does
  not record rested voltage. A future OCV correction requires separately
  collected rest points (or a validated resistance model) and remains deferred.
- Record the calibration load and optional temperature metadata because usable
  capacity and loaded voltage depend on discharge rate and temperature.
- Treat a profile from the 50 ohm test as a good low-rate baseline, not a
  perfect model of every higher, dynamic system load.

### Remaining budget and time to depletion

Report both:

- remaining charge in mAh;
- remaining usable energy in mWh.

The latter is the remaining energy budget; power is the instantaneous rate at
which that budget is consumed.

Compute time to depletion from remaining energy and a smoothed positive load:

```text
timeToEmptyHours = remainingEnergyMWh / averagePowerMw
```

- Use a multi-minute moving average or exponential moving average so load
  spikes do not make the prediction unusably volatile.
- Return an explicit unknown/unavailable state when the load is below the
  meaningful threshold, current is negative, initial SOC is uncertain, or no
  valid sample has arrived recently.
- Expose estimator confidence separately from the numerical SOC/TTE values.

## Calibration state machine

```text
Idle -> ArmedFull -> Discharging -> Finalizing -> Complete
                 \-> Invalid
                 \-> Aborted
```

### Idle

- No calibration is active.
- Runtime estimation may use the most recent completed compatible profile.

### ArmedFull

- Enter only through an explicit command/API call.
- Require the expected 7S full-voltage window and positive discharge current.
- Record configuration, pack identifier, initial voltage/current, and a new
  calibration session identifier.
- Do not silently accept a clearly partial pack as fully charged.

### Discharging

- Consume every successful main-monitor sample.
- Integrate actual charge and energy; never assume the 50 ohm load produces
  constant current.
- Aggregate measurements in RAM and append one summary record per minute.
- Track sample count, minimum/maximum voltage, average voltage/current/power,
  cumulative mAh/mWh, and maximum sample gap for data-quality assessment.
- Invalidate the run on charging current, excessive sample gap, sensor failure
  beyond tolerance, filesystem persistence failure, or explicit interruption.

### BMS cutoff recognition

- Finalize on an abrupt transition from a valid discharge to sustained near-zero
  pack voltage/current consistent with the BMS opening.
- Require a short dwell so a transient load disconnect is not mistaken for the
  BMS cutoff.
- Preserve the last valid under-load voltage separately from the first
  disconnected reading.
- Store the observed cutoff behavior as profile metadata.
- Do not control, bypass, or replace the BMS.

### Finalizing and Complete

- Flush the final aggregate.
- Append the measured usable mAh, measured usable mWh, elapsed time, observed
  cutoff, quality statistics, and completion marker.
- Normalize the discharge records into SOC points only after the total measured
  capacity is known:

```text
SOC(point) = 1 - cumulativeDischargedCharge(point) / totalDischargedCharge
```

- Produce a compact curve of at most 101 points, one per percentage of SOC.
- Activate a profile only when its header, records, completion marker,
  configuration identity, and CRCs all validate.

### Invalid and Aborted

- Keep partial data for diagnostics but never use it as an active fuel-gauge
  profile.
- Record a reason code such as sample gap, sensor error, charging detected,
  storage error, reboot, user abort, or load removed.
- A reboot during an active calibration makes the run incomplete/invalid even
  if the append journal itself is recoverable. Battery relaxation during the
  gap makes transparent continuation misleading.

## Sampling and storage cadence

- Keep the INA sampling cadence independent, initially 100 ms to 1 s.
- Integrate each accepted sample in RAM.
- Form intermediate calibration observations at approximately ten per minute
  if useful for stability checks.
- Persist one averaged record per minute. This gives approximately 1,440
  records per 24-hour run.
- A fixed 24-32 byte record yields roughly 35-46 KiB per 24 hours, comfortably
  within the existing approximately 1.9 MiB LittleFS partition.
- Flush or close after every minute record. Do not keep an entire calibration
  only in RAM.

## LittleFS journal format

Use a versioned, append-only binary journal so implementation can use the
existing `openAppend`, `write`, `flush`, `close`, and chunk-reader APIs without
adding shared filesystem mutation primitives.

Logical records:

1. `SessionHeader`
   - magic and format version;
   - session and pack identifiers;
   - battery configuration hash;
   - sampling/persistence parameters;
   - expected capacity and load metadata;
   - record size and CRC.
2. `IntervalRecord`
   - elapsed seconds;
   - average/minimum/maximum bus millivolts;
   - average current and power;
   - cumulative discharged mAh and mWh;
   - sample count and maximum gap;
   - CRC.
3. `ProfilePoint`
   - SOC index;
   - loaded voltage;
   - representative current;
   - remaining mAh and mWh;
   - CRC.
4. `SessionFooter`
   - completion/invalid status and reason;
   - measured totals and duration;
   - observed BMS cutoff metadata;
   - record counts and whole-session checksum.

On boot, scan records sequentially, reject a partial or bad trailing record,
and select the most recent completed compatible session. Old sessions may stay
in the same journal until explicitly removed; existing `/cat` and `/rm`
commands provide basic inspection and cleanup. A later export command can
render completed records as CSV without making CSV the durable on-device
format.

## Metrics and status API

Keep metric names within the existing eight-character limit. Candidate 32-bit
metrics:

| Metric | Type | Meaning |
| --- | --- | --- |
| `socPpt` | gauge | state of charge in parts per thousand |
| `remMah` | gauge | remaining charge in mAh |
| `remMwh` | gauge | remaining usable energy in mWh |
| `usedMah` | gauge | discharged charge in current session |
| `usedMwh` | gauge | discharged energy in current session |
| `avgMw` | gauge | smoothed positive load in mW |
| `tteMin` | gauge | predicted minutes to depletion, zero when unavailable |
| `conf` | gauge | estimator confidence/state code |
| `calStat` | gauge | calibration state code |
| `calPts` | counter | persisted calibration interval count |
| `gapMax` | gauge | maximum accepted sample gap in ms |
| `fail` | counter | measurement/integration failures |
| `fsFail` | counter | persistence failures |

The semantic status API must distinguish a genuine zero from unavailable data;
metrics that must remain numeric can use companion validity/confidence fields.

## Commands

Add battery-monitor commands following the existing command backend:

- `/battery status` - report configuration, source state, SOC, remaining
  charge/energy, average load, TTE, confidence, and active profile metadata.
- `/battery calibrate start` - arm a new full-pack discharge calibration after
  validating voltage/current/filesystem state.
- `/battery calibrate abort` - append an aborted footer and stop calibration.
- `/battery calibrate status` - report duration, samples, gaps, persisted
  points, used mAh/mWh, current voltage, and last storage error.
- `/battery profiles` - list complete and incomplete sessions found in the
  journal.

Profile deletion remains an explicit destructive operation through the
existing filesystem command unless a narrowly scoped battery command is later
justified.

## Implementation phases

Current staging: initial component integration and hardware checks use the
breadboarded single-INA226 `scratch` environment. The eventual two-monitor
`power` node integration remains Phase 6 work after general board bring-up and
physical address-strapping confirmation.

### Phase 1 - Interfaces and pure estimator math

- [x] Add this maintained plan before implementation.
- [x] Add `BatteryMeasurement`, chemistry/configuration, state, status, profile,
  and callback interface types.
- [x] Validate 7S4P values, ordered per-cell thresholds, nominal capacity,
  timing intervals, paths, and 32-bit output ranges.
- [x] Implement rollover-safe trapezoidal charge/energy integration with
  retained fixed-point remainders.
- [x] Implement default idealized voltage initialization, confidence, load
  smoothing, and time-to-empty calculation.
- [x] Add focused compile-time or host-side tests for arithmetic boundaries,
  sample gaps, zero/negative current, timestamp rollover, and TTE validity.

### Phase 2 - INA2xx sample delivery

- [x] Add allocation-free successful-sample callback registration to INA2xx.
- [x] Define callback ordering relative to latest sample, metrics, alerts, and
  limit error returns.
- [x] Verify one callback per valid sample and no callback on failed samples.
- [x] Document that callbacks must not block or access LittleFS.

### Phase 3 - Calibration state machine and aggregation

- [x] Implement explicit arm/start, discharge, BMS-cutoff dwell, finalize,
  invalid, and abort transitions.
- [x] Qualify the full 7S pack before accepting a calibration start.
- [x] Aggregate frequent input samples into minute records without losing
  integration precision.
- [x] Detect excessive sample gaps, current reversal, load removal, sensor
  failure, and storage failure.
- [x] Produce measured mAh/mWh totals, observed cutoff metadata, and the compact
  normalized SOC curve.

### Phase 4 - LittleFS persistence

- [x] Define packed, endian-stable journal records with magic, version, explicit
  lengths, and CRC.
- [x] Implement append-and-flush from `work()` at the configured interval.
- [x] Implement bounded-memory sequential journal loading and validation.
- [x] Ignore partial trailing records and reject incomplete/invalid sessions as
  active profiles.
- [ ] Verify storage-full, short-write, corrupt-record, incompatible-version,
  and reboot-during-calibration behavior.

### Phase 5 - Metrics, commands, and documentation

- [x] Add 32-bit battery estimator/calibration metrics and validity/confidence
  reporting.
- [x] Add status, calibration start/abort/status, and profile-list commands.
- [x] Document configuration, units, calibration wiring, BMS-cutoff semantics,
  storage format, expected test duration, and limitations.
- [x] Document that pack-level monitoring cannot diagnose a weak individual
  series group and never replaces the BMS.

### Phase 6 - Power-node integration

- [ ] Configure one I2C master for U8 and U9 and assign/document distinct INA226
  addresses.
- [ ] Instantiate U8 as the main battery source and U9 as the informational 5 V
  rail monitor.
- [ ] Feed only U8 successful samples into `BatteryMonitor`.
- [ ] Publish U9 metrics and independent logic-rail error states without
  affecting battery SOC.
- [ ] Confirm USB source selection keeps the power node, I2C pull-ups, INA logic
  supplies, and LittleFS alive while J6 discharges the pack.

### Phase 7 - Verification

- [x] Replay deterministic synthetic discharge traces and compare integrated
  mAh/mWh against analytically calculated totals.
- [x] Build only the currently relevant `scratch` environment and inspect its
  SARIF output.
- [ ] After production integration, build only the `power` environment and
  inspect its SARIF output.
- [ ] Verify two physical INA226 devices coexist at their configured addresses.
- [ ] Perform a short bench run to validate sample delivery, journal appends,
  commands, and abort handling before starting a full pack discharge.
- [ ] Perform the uninterrupted 50 ohm calibration while logic is USB-powered.
- [ ] Confirm observed BMS cutoff finalizes a valid profile and survives reboot.
- [ ] Compare learned mAh/mWh with the claimed 10 Ah value and retain the raw
  calibration session for inspection.
- [ ] Start a subsequent runtime session using the learned profile and verify
  SOC, remaining energy, confidence, and TTE behavior under changing load.
- [ ] Record all results and remaining accuracy gaps in this plan.

## Planned file scope

- `include/BatteryMonitor/Interfaces/` - public measurement, configuration,
  profile, state, and status types.
- `include/BatteryMonitor/detail/` - estimator, integration, calibration state
  machine, profile journal, metrics, and commands.
- `include/BatteryMonitor/Facade.hpp` - public component export.
- `include/Wire/I2C/Interfaces/Ina2xxConfig.hpp` and
  `include/Wire/I2C/detail/Ina2xx.hpp` - successful-sample callback only.
- `src/scratch/config.hpp` and `src/scratch/main.cpp` - current breadboard
  integration for the single INA226 at `0x40`.
- `src/power/config.hpp` and `src/power/main.cpp` - deferred two-monitor wiring
  and main battery-monitor integration after production board bring-up.
- `docs/wire-i2c.md`, `docs/overview.md`, and battery-monitor documentation -
  API, operation, calibration procedure, and limitations.
- This plan - maintained checklist and verification record.

## Known limitations and deferred improvements

- Pack voltage cannot reveal an imbalanced or weak individual series group;
  that remains the BMS's responsibility.
- The first learned curve is load- and temperature-specific. Multiple profiles
  or an impedance/temperature model may be added later without changing the
  measurement callback boundary.
- No charging/coulomb-efficiency model is included. Packs are treated as
  externally charged and a calibration begins from an explicitly full pack.
- No cell-temperature sensor is currently available. The calibration should at
  least record user-provided ambient/test conditions in its metadata.
- INA228 is the production target, but its native energy/charge register backend
  remains out of scope for the INA226-based first test. Its semantic capability
  surface is protected rather than treated as speculative.
- Runtime estimates are operational guidance, not a safety mechanism. Hardware
  BMS protection and appropriate load wiring remain mandatory.

## Verification record

- Contract-alignment implementation completed on 2026-08-26. The core now has
  single-owner runtime state, an atomic one-request command mailbox, a
  zero-wait published status snapshot, explicit freshness/storage health,
  optional TTE, one-shot failed writes, bounded journal growth, and incremental
  four-record finalization steps. Commands are registered through an explicit
  optional adapter.
- `bin/test-battery-monitor` builds with C++23, `-Wall -Wextra -Werror`, and
  passes fixed-point EMA decay, charging/staleness invalidation, valid zero TTE,
  sample gaps, timestamp rollover, calibration pending-write consumption,
  journal CRC/format, complete-profile activation, partial-write/reboot recovery,
  record bounds, and injected open/write/short-write/flush/close outcomes.
- The final contract-aligned `scratch` image builds with no SARIF diagnostics at
  75,948 bytes RAM (23.2%) and 744,928 bytes flash (35.5%). Compared with the
  pre-alignment image below, that is 24 bytes less RAM and 4,824 bytes more
  flash. `batteryMonitor` is 3,640 bytes (80 bytes smaller) and `ina226` remains
  696 bytes. This image has not been uploaded; live verification is the next
  battery-test step.

- Implementation iteration started on 2026-08-26. Battery-dependent checks are
  intentionally deferred until the pack has charged; the current hardware
  scope is configuration, zero-input behavior, persistence initialization, and
  command/boot validation on `env:scratch` only. The production `power` node is
  not connected and will not be flashed during this iteration.
- `env:scratch` built successfully with no SARIF diagnostics and was flashed to
  ESP32-C3 `80:f1:b2:8b:09:48`. Firmware uses 75,972 bytes RAM (23.2%) and
  740,104 bytes flash (35.3%) after the final journal compatibility and
  arithmetic-boundary checks.
- Live battery-free checks confirmed an empty, mounted LittleFS; INA226
  sampling at `0x40`; `/battery status` reporting `Absent`, zero budget, and
  unavailable TTE; `/battery profiles` reporting zero complete, incompatible,
  incomplete, and corrupt sessions; and calibration start being rejected as
  `NotFull` without creating a journal.
- The unloaded INA226 showed approximately +/-2.5 mA conversion noise. The
  scratch reverse-current practical limit now matches the 5 mA estimator
  deadband, eliminating warning chatter without hiding meaningful charging.
- Deterministic compile-time traces verify one hour at 1 A integrates to
  1,000 mAh and one hour at 1 W integrates to 1,000 mWh while retaining
  fractional remainders. Additional checks cover rollover, voltage/SOC bounds,
  deadband signs, saturation, and TTE division.
- Focused schematic analysis on 2026-08-26 confirmed U8 covers J5-to-U7/J6 and
  U9 covers the buck-to-5 V source-select path.
- Existing LittleFS provides append, byte-span write, flush/close, chunked
  reading, file inspection, and explicit removal; the journal design fits these
  capabilities without a shared filesystem refactor.
- Physical full-discharge calibration, observed BMS cutoff, learned capacity,
  and runtime prediction remain pending battery-dependent verification.
