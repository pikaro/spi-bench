# Battery monitoring and discharge calibration

`BatteryMonitor` is the sensor-independent battery fuel-gauge layer. It accepts
timestamped pack voltage, current, and power measurements, integrates consumed
charge and energy, and can create a learned discharge profile on LittleFS. The
INA2xx driver remains responsible for I2C, conversion, and electrical limits.

## Configured nodes

The breadboarded `scratch` node uses:

- INA226 address: `0x40`
- SDA: GPIO10
- SCL: GPIO20 (`Pin::RX` on the ESP32-C3 SuperMini)
- shunt resistance: 2 milliohms
- battery model: standard 4.2 V Li-ion, 7S4P
- provisional capacity: 10,000 mAh
- pack ID: `1` for the first physical test pack
- calibration load metadata: 50 ohms
- journal path: `/battery.bin`

The v2 `power` node uses a 100 kHz externally pulled I2C bus on GPIO7 SDA and
GPIO8 SCL. Its two required INA226 modules are:

| Rail | Address | Shunt | Practical high current | Absolute high current | Battery source |
| --- | --- | --- | --- | --- | --- |
| 24 V, U8 | `0x40` | 2 mOhm | 2 A | 3 A | yes |
| common 5 V, U9 | `0x41` | 2 mOhm | 500 mA | 1 A | no |

On both INA226 modules, `C+` and `C-` are the high-current terminals of the
fitted shunt: connect `C+` upstream and `C-` downstream for positive load
current. `V+` is the INA226 VBUS input, while `V-` is tied to module ground.
The exposed `IN+` and `IN-` nodes are after the module's series 4.7 ohm input
resistors and are intentionally left externally unconnected.

U8 alone feeds BatteryMonitor; U9 retains the ordinary INA sampling, limit,
and metrics behavior. The Power pack is configured as 7S4P with a provisional
6,000 mAh capacity, a 21.0--29.4 V practical window, and approximately 20--30 V
absolute error boundaries. The stored strict-comparison boundaries are
20.006 V and 29.995 V so measured 20.000 V and 30.000 V are errors.

Power still carries the initial default `packId` and 50 ohm calibration-load
metadata. Assign the real pack identity and finalized calibration fixture
metadata before starting a production calibration.

`packId` identifies the physical battery, not merely its 7S4P design. Assign a
stable, distinct nonzero ID before calibrating each pack. The ID participates in
profile compatibility, so a profile learned from one pack is not silently used
for another pack configured with a different ID. Multiple pack histories can
share the journal; only the newest completed profile compatible with the active
configuration is selected.

## Measurement semantics

Positive current means discharge. Current more negative than the configured
deadband is charging; charging is not counted and invalidates an active
discharge calibration. The scratch INA reverse-current warning begins at
-5 mA so unloaded conversion noise does not repeatedly toggle the warning.

The breadboarded scratch configuration's 7S voltage states are:

| Pack voltage | State | Meaning |
| --- | --- | --- |
| 0-1.0 V | absent | pack disconnected or BMS open |
| above 1.0 V, below 17.5 V | absolute under | erroneous or unsafe live pack voltage |
| 17.5-21.0 V | practical under | warning; calibration may continue toward BMS cutoff |
| 21.0-29.4 V | normal | expected operating window |
| above 29.4 V, up to 30.1 V | practical over | warning |
| above 30.1 V | absolute over | error |

Zero volts is deliberately a distinct disconnected state rather than an
absolute cell-undervoltage error. This permits the BMS opening to complete a
calibration without producing a stream of sensor-work failures.

The INA sample callback runs synchronously after a successful conversion has
updated the driver's latest sample, software-limit state, and metrics. Practical
and absolute states remain successful observations rather than operational
errors. The callback must remain bounded and must not access LittleFS.
`BatteryMonitor::work()` owns all filesystem operations.

`observe()` and `work()` are non-reentrant and must be called by the same owner
task. Commands publish one fixed-size start/abort request to an atomic mailbox;
they never mutate estimator or calibration state directly. The owner consumes
at most one request per `work()` call. Status readers copy a fixed-size
published snapshot under a short lock, while owner publication uses zero wait
and therefore cannot stall the INA callback.

Public measurement/configuration aggregates use explicit unit-suffixed fields
and are passed as semantic objects rather than adjacent scalar arguments. This
keeps the initial embedded interface trivially copyable and zero-overhead; a
project-wide scalar quantity-wrapper migration is intentionally not coupled to
the first battery test.

## Runtime estimate

Without a completed compatible profile, startup SOC is a low-confidence linear
estimate between the configured practical minimum and maximum pack voltages.
Subsequent discharge is integrated from actual samples using fixed-point
trapezoidal integration:

```text
charge [mAh] = average current [uA] * elapsed [ms] / 3,600,000,000
energy [mWh] = average power [mW] * elapsed [ms] / 3,600,000
```

Fractional remainders are retained so frequent samples do not lose sub-mAh or
sub-mWh contributions. Public values and metrics remain 32-bit; checked 64-bit
values are used only for bounded intermediate products and remainders.

A completed profile replaces the node's provisional capacity and nominal-energy
totals (10 Ah on scratch, 6 Ah on Power).
Its voltage curve was measured under the calibration load, not at open circuit.
It is therefore used for startup matching only when the observed current is
within 25% of the recorded representative current. Other startup conditions
fall back to the idealized voltage estimate. No rested-voltage correction is
performed without separately collected OCV data.

Time to depletion uses remaining mWh and a five-minute fixed-point exponential
average whose signed division remainder is retained. Small adjustments
therefore accumulate instead of freezing at an integer residual. TTE is an
optional value and is explicitly unavailable when no source is present, the
measurement is stale, charging is detected, the estimate is not initialized,
or the load is below 100 mW. A present value of zero minutes remains distinct
from unavailable.

After a pack disconnect/BMS-open state, the next valid pack measurement starts
a new discharge session and reinitializes its budget from voltage. Charging a
still-connected pack is deliberately not modeled; disconnect it while using the
external charger so reconnection establishes a new session.

## PubSub status and battery gauge

The power node publishes `BatteryStatusEvent` on the existing `Power` topic
once per second. The compact snapshot carries state of charge in parts per
thousand plus source, freshness, and confidence state. Master caches the latest
snapshot but only treats it as displayable when the source is usable, the
measurement is fresh, and estimator confidence is available.

Selecting `Battery` in the IO main menu starts the shared 500 ms radial gauge
on the GPU UI layer. It maps `0..1000` state-of-charge parts per thousand over
32 spokes with a red-to-green spectrum, then uses the UI layer's shared
two-second fade. An unavailable or stale estimate is logged and is not rendered
as a misleading empty battery.

## Commands

```text
/battery status
/battery calibrate start
/battery calibrate status
/battery calibrate abort
/battery profiles
```

`calibrate start` requires all of the following:

- LittleFS is mounted;
- storage health is `Healthy` and a complete bounded session fits;
- a valid sample has been received;
- pack voltage is at least the configured 28.0 V full-qualification threshold
  (4.0 V per cell) and no higher than the configured absolute maximum;
- discharge current is at least 25 mA.

The command queues one request for `BatteryMonitor::work()`; a second pending
request is rejected as backpressure rather than overwriting the first. An
unsuccessful start does not create a file. On the present battery-free scratch
setup it is expected to fail because no qualifying full-pack sample exists.
Every immediate start rejection reports its specific
`BatteryCalibrationInvalidReason`, such as `NotFull`, `NoDischargeLoad`,
`SensorTimeout`, or `StorageUnavailable`, rather than echoing the unchanged
calibration state and its previous reason. Mailbox and lifecycle failures report
their `ReturnCode` because they are not calibration-invalidity reasons.

## Full-pack calibration procedure

The first hardware commissioning uses one bounded 15-minute rehearsal before
the real full discharge. The exact workflow and pass/fail criteria are in the
[BatteryMonitor initial validation plan](battery-monitor-initial-test-plan.md).

During calibration, every successfully flushed interval automatically emits
two timestamped records: session/interval timing and sampling quality, followed
by voltage/current/power and cumulative mAh/mWh. Completion emits capacity,
energy, duration, interval count, maximum sampling gap, and cutoff voltages.
Periodic command polling is neither required nor part of the validation plan.

For a routine full calibration, fully charge the pack, power the ESP32/INA
logic independently, connect the established fixed load, inspect
`/battery status` once, and issue `/battery calibrate start` once. Do not reset,
reflash, remove the load, or charge the pack during the run. Five seconds of
sustained near-zero voltage/current after the BMS opens triggers automatic
finalization. Validate the downloaded journal after `Complete` with
`bin/validate-battery-calibration`.

At the claimed 10 Ah capacity, a 50 ohm discharge should take roughly 18-22
hours; the actual current falls with voltage and the monitor integrates it
rather than assuming a constant value. The resistor begins near 588 mA and
17.3 W at 29.4 V, within the stated 50 W fixture rating, but the setup must
remain physically clear and thermally supervised.

## Persistence and recovery

The version-2 journal is an append-only sequence of 64-byte, little-endian
records. Every
record contains `BMON` magic, a format version, record type, sequence/session
identity, fixed-length payload, and CRC-32. Record types are session header,
minute interval, normalized profile point, and terminal footer.

A valid completed profile has a compatible configuration hash, continuous
record sequence, 101 SOC points, completion footer, per-record CRCs, and a
session checksum. Profile-point records store only loaded voltage and
representative current; remaining capacity is derived from point SOC and the
footer totals rather than persisted redundantly.

The default 36-hour calibration bound permits 2,161 one-minute interval records
including a final partial interval. Header, 101 profile points, and footer make
the worst-case session 2,264 records (144,896 bytes). The complete journal is
limited to 16,384 records (1,048,576 bytes). A new session is rejected before
its header when that entire worst case cannot fit either the journal bound or
currently free LittleFS space. Existing profiles are never deleted
automatically.

A partial trailing record remains readable as end-of-journal but changes
storage health to `Corrupt`, because appending behind it would lose fixed record
alignment. Corrupt, oversized, aborted, invalid, or reboot-interrupted sessions
are never selected as learned profiles. A compatible interrupted session gets
one invalid-footer attempt on the first `work()` after reboot. Any write,
flush, or close failure consumes its pending operation, marks storage
`WriteFailed` (or `Full`/`Corrupt` where identifiable), invalidates calibration,
and is not retried every loop.

Finalization is incremental. Each `work()` reads at most four journal records,
each interval checks the fixed 101 SOC targets, and later calls append at most
one profile point or footer. A profile becomes active only after the complete
footer append, flush, and close succeeds.

The current ESP32 filesystem platform still uses `std::FILE`. Heap behavior and
open/append/flush/close latency must be measured during the first hardware test;
replacement of that backend remains a separate, explicitly tracked change.

## Limitations

- Pack-level voltage cannot identify an imbalanced or weak individual series
  group and never replaces the BMS.
- The learned curve is specific to the discharge load and temperature. The
  current hardware has no pack-temperature measurement.
- Charging and coulombic efficiency are outside the current model.
- The v2 logic board uses INA226 modules. The semantic
  temperature/energy/charge capability surface remains available for a future
  INA228 backend, but those values are unavailable from INA226.
- Battery-dependent cutoff, capacity, curve quality, reboot recovery, and TTE
  checks remain pending until the pack is charged.
