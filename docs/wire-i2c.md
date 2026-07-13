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

These devices intentionally do not share the legacy API. They keep the working
command sequences but replace singleton registries, `shared_ptr`, `unordered_map`,
and `vector` payload construction with value ownership and fixed-size buffers.

## Media Debug Display

`env:media` now enables the ESP-IDF I2C driver because the optional FFT debug
display compiles through the media source root. Runtime display startup is
guarded by `enableFftDebugDisplay` in `src/media/config.hpp`, which defaults to
`false` until hardware is wired.

When enabled, the media node starts one `Wire::I2C::Master`, an SSD1306
display, and `AudioFft::FftDisplay`. FFT callbacks only copy the latest scaled
band frame into a small value slot; the separate display task performs the slow
I2C framebuffer flush at its configured refresh interval.
