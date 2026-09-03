# I2C Wire Layer

`include/Wire/I2C/` owns the ESP32 I2C master bus wrapper and the first
allocation-free I2C device drivers.

## Component Shape

- `Wire/I2C/Facade.hpp` exports the public master and device classes.
- `Wire/I2C/Interfaces/` contains value-type configs and small enums.
- `Wire/I2C/detail/Master.hpp` owns one I2C master bus instance.
- `Wire/I2C/detail/Device.hpp` is the reusable value-owned device handle used
  by concrete devices.
- `Wire/I2C/detail/platform/PlatformESP32.hpp` binds the master to ESP-IDF's
  `driver/i2c_master.h` API.

The master uses a fixed-capacity device table (`maxDevicesPerMaster`, currently
32) instead of heap-backed maps. Each registered device gets a small
`DeviceHandle`; reads and writes are serialized through the master's mutex.
This keeps the bus suitable for many low-speed peripherals without making the
I2C component a singleton or tying it to a specific device type.

The master also registers metrics in `include/Wire/I2C/detail/Metrics.hpp`.
`i2cCore` counts transaction failures, bus timeouts, and mutex timeouts;
`i2c` counts registered devices, read/write/write-read operations, and bytes;
`i2cProf` holds opt-in transaction duration totals and max latency.

## Devices

The first concrete devices are:

- `Ssd1306Display`: 128-wide monochrome SSD1306 display support with a fixed
  framebuffer and small drawing primitives.
- `Pcf8574`: PCF8574-style 8-bit quasi-bidirectional GPIO expander.
- `Mcp4661`: MCP4661 dual digital potentiometer with volatile wiper writes,
  EEPROM writes, increment/decrement commands, status reads, and TCON access.
- `Ina2xx`: model-selected INA219/INA226 current and voltage monitoring, with a
  protected INA228 capability surface, periodic metrics, signed
  shunt/current/power values, independent bus-voltage and current operating
  windows, and capability-checked INA226 ALERT-pin support.

These devices intentionally do not share the legacy API. They keep the working
command sequences but replace singleton registries, `shared_ptr`, `unordered_map`,
and `vector` payload construction with value ownership and fixed-size buffers.

## INA2xx Current/Voltage Monitor

`Ina2xx` is constructed with an explicit `Ina2xxModel`. INA219 and INA226 share
the semantic interface but keep their register scaling, configuration, and
identity behavior model-specific. INA226 startup verifies manufacturer and die
IDs. INA219 has no identity register, so choosing INA219 states what is expected
at the configured address rather than proving the silicon model.

INA228 is the intended production target and is represented explicitly in the
model and capability API. Its native 20-bit/24-bit/40-bit register backend is
not implemented yet, so `begin()` with `Ina228` returns `NotSupported` instead
of routing it through the incompatible INA219/INA226 16-bit helpers.

The driver runs both devices in continuous shunt-and-bus conversion mode.
`work(nowMs)` reads at `sampleIntervalMs`, using rollover-safe unsigned elapsed
time. `sampleNow(nowMs)` forces a read and returns the acquired sample through
`std::expected<Ina2xxSample, ReturnCode>`. A failed transaction or device math
overflow increments failure metrics and preserves the last valid sample;
`latestSample()` returns `NotFinished` until the first valid sample exists.

Configuration requires:

- a seven-bit INA address from `0x40` through `0x4f`;
- a nonzero sampling interval;
- shunt resistance in micro-ohms;
- expected maximum current in microamps, used to choose a representable native
  current LSB, calibration register value, and INA219 shunt gain;
- absolute and practical bus-voltage limits in millivolts; and
- absolute and practical signed-current limits in microamps.

Each window must satisfy:

```text
absoluteMin <= practicalMin <= practicalMax <= absoluteMax
```

The absolute minimum must be lower than the absolute maximum, but adjacent
limits may be equal. This permits a bus window whose absolute and practical
minimum are both zero. INA219 bus limits cannot exceed 26 V; INA226 bus limits
cannot exceed 36 V. Current limits must fit both the calibrated current-register
range and the shunt-input range implied by `shuntMicroOhms`.

Calibration starts with the smallest current LSB that covers the expected
maximum current, then increases that LSB when necessary to fit the selected
model's calibration field. In particular, INA226 bit 15 is reserved, so its
calibration value must fit `FS[14:0]`; the driver never relies on the device
silently discarding an oversized high bit.

Practical crossings in either window log a warning and absolute crossings log
an error. Both are successful measurements represented by `Ina2xxLimitState`;
they do not turn `work()` or `sampleNow()` into repeated operational failures.
Logs and the optional limit callback are emitted only when that measurement's
classified state changes; `Ina2xxLimitEvent::measurement` distinguishes
`BusVoltage` from `Current`. Each measurement independently emits a `Normal`
transition when it returns to its practical window. Raw shunt voltage remains
available in samples and metrics, but is not an application limit.

The INA226 hardware ALERT facility is deliberately separate from that software
window. The chip has one alert-limit register, so `hardwareAlert.function`
selects exactly one of `BusVoltageOver`, `BusVoltageUnder`, `CurrentOver`, or
`CurrentUnder`. Bus functions program the corresponding practical bus limit.
Current functions convert the corresponding practical current limit to a shunt
threshold using `shuntMicroOhms`; callers do not configure raw shunt drop.
Software still checks both directions of both windows on every sample. GPIO
handling is deferred through `DigitalInput`; the GPIO callback only marks work
pending and I2C reads plus the user callback run from `work()` context.

`setAlertCallback()` immediately returns `NotSupported` for INA219. For INA226,
the callback must be installed before `begin()`, and the begin config must
contain the matching hardware GPIO configuration. Limit callbacks are
available on both models and must likewise be changed only while inactive.

`setSampleCallback()` is available on both models for downstream semantic
consumers such as `BatteryMonitor`. It runs exactly once for every successfully
converted sample, after the latest sample, limit state, and metrics are updated.
It runs for practical and absolute states but not after an I2C or device-math
failure. The callback executes synchronously in `work()` context, must be
bounded and non-blocking, and must never perform filesystem I/O.

An abbreviated setup looks like this:

```cpp
Totem::Wire::I2C::Master i2c;
Totem::Wire::I2C::Ina2xx railMonitor{
    i2c, Totem::Wire::I2C::Ina2xxModel::Ina226};

Totem::Wire::I2C::Ina2xxConfig railConfig{
    .device = {.address = 0x40},
    .sampleIntervalMs = 100,
    .shuntMicroOhms = 2'000,
    .expectedMaxCurrentMicroamps = 2'000'000,
    .busVoltage = {
        .absoluteMinMillivolts = 0,
        .practicalMinMillivolts = 0,
        .practicalMaxMillivolts = 29'400,
        .absoluteMaxMillivolts = 30'000,
    },
    .current = {
        .absoluteMinMicroamps = -2'000'000,
        .practicalMinMicroamps = 0,
        .practicalMaxMicroamps = 1'800'000,
        .absoluteMaxMicroamps = 2'000'000,
    },
    // Set hardwareAlert here only when an ALERT GPIO is wired.
    .metricsGroupName = "rail0",
};

REPORT_IF_ERR(
    railMonitor.setLimitCallback(
        [](const Totem::Wire::I2C::Ina2xxLimitEvent &event) {
            handleRailLimit(event);
        }),
    "Failed to set rail limit callback");
REPORT_IF_ERR(
    railMonitor.setSampleCallback(
        [](const Totem::Wire::I2C::Ina2xxSample &sample) {
            forwardSuccessfulSample(sample);
        }),
    "Failed to set rail sample callback");
REPORT_IF_ERR(railMonitor.begin(railConfig), "Failed to start rail monitor");

// Call from the owning task loop.
REPORT_IF_ERR(railMonitor.work(nowMs), "Rail monitor work failed");
```

Each instance registers the configured metrics group name, which must be unique
and at most eight characters. Current INA219/INA226 metrics are:

| Metric | Type | Meaning |
| --- | --- | --- |
| `shuntUv` | signed gauge | shunt voltage in microvolts |
| `busMv` | gauge | bus voltage in millivolts |
| `currUa` | signed gauge | calibrated current in microamps |
| `powerMw` | signed gauge | bus voltage times signed current in milliwatts |
| `tempMc` | signed gauge | future temperature; zero on INA219/INA226 |
| `energyMj` | gauge | future energy; zero on INA219/INA226 |
| `chargeMc` | signed gauge | future charge; zero on INA219/INA226 |
| `cap`, `valid` | gauges | supported and currently valid capability masks |
| `busState`, `curState` | gauges | independent bus-voltage/current window states |
| `ageMs` | gauge | last-sample age |
| `samples`, `fail`, `ovf`, `alerts` | counters | sampling and alert health |

Capability and validity semantics are:

| Model | Implemented | Supported capability surface | Per-sample validity |
| --- | --- | --- | --- |
| INA219 | yes | shunt, bus, current, power | those four bits after a successful sample |
| INA226 | yes | INA219 set plus identity and hardware ALERT | measurement bits after a successful sample; identity/ALERT are device capabilities |
| INA228 | backend pending | INA226 set plus temperature, energy, and charge | each measurement/accumulator bit will be set only when that sample contains a valid native reading |

Zero temperature, energy, or charge is therefore a legitimate future INA228
value. Availability is determined from `validCapabilities`, never from zero.

Metric storage remains 32-bit. Checked 64-bit temporaries are used only while
scaling products and calibration constants; the driver adds no 64-bit metric
or accumulator.

INA228 is not implemented. The shared API is semantic rather than based on a
common 16-bit register map, leaving a future INA228 backend free to handle its
24-bit measurements, 40-bit accumulators, temperature, energy, charge, and
different alert model. Future large values must be scaled or saturated into
explicit 32-bit metric units rather than widening the metrics backend.

Register encodings and scaling were checked against the official TI
[INA219 datasheet](https://www.ti.com/lit/ds/symlink/ina219.pdf) and
[INA226 datasheet](https://www.ti.com/lit/ds/symlink/ina226.pdf).

## Media Debug Display

`env:media` now enables the ESP-IDF I2C driver because the optional FFT debug
display compiles through the media source root. Runtime display startup is
guarded by `enableFftDebugDisplay` in `src/media/config.hpp`, which defaults to
`false` until hardware is wired.

When enabled, the media node starts one `Wire::I2C::Master`, an SSD1306
display, and `AudioFft::FftDisplay`. FFT callbacks only copy the latest scaled
band frame into a small value slot; the separate display task performs the slow
I2C framebuffer flush at its configured refresh interval.
