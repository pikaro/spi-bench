# Audio

`include/Audio/` owns the media-node audio input and FFT analysis path.

## Component Shape

- `Audio/Facade.hpp` exports the public audio classes.
- `Audio/Interfaces/Types.hpp` contains value-type configuration and callback
    payloads for I2S input, FFT frames, magnitude scaling, and beat events.
- `Audio/detail/I2SSource.hpp` owns the single active I2S source.
- `Audio/detail/FftAnalyzer.hpp` feeds the source into arduino-audio-tools'
    stream/sink FFT path, calculates fixed FFT bands, updates the magnitude
    cache, and emits frame/beat callbacks.
- `Audio/detail/platform/PlatformESP32.hpp` is the component-owned ESP32
    platform layer for arduino-audio-tools I2S and FFT types.

There is intentionally no audio source directory. The current design supports
one simultaneous audio source, which keeps ownership and runtime scheduling
simple.

## I2S Devices

`I2SSourceConfig::device` selects a preset from the component device library.
The first preset is `I2SDevicePreset::LegacySoundCard`, copied from
`legacy/include/Pins.hh`:

- bit clock: GPIO10
- word select: GPIO21
- data input: GPIO20

`I2SDevicePreset::Custom` uses `I2SSourceConfig::customDevice` for hardware
that does not match a preset.

The current `env:media` board is `custom_esp32_nodemcu` through
`platformio.ini`. The legacy preset pins are not a safe classic ESP32 NodeMCU
pinout, so hardware validation still needs a board decision or a custom device
config before unattended runtime use.

## FFT Notes

FFT band magnitudes are reduced exactly once per band, then optional weighting
and manual gains are applied. This avoids the legacy double-averaging bug where
band sums were averaged once in the callback and then divided by bin count again
during scaling.

The magnitude cache is frame-driven instead of task-driven. It tracks adaptive
floor and peak values per band and writes scaled `0..255` values into each
`FftFrame`.

The beat tracker is an energy-onset detector over configured scaled FFT bands.
It reports detected beats and a bounded BPM estimate. Higher-level musical beat
prediction is intentionally not implemented yet because reliable prediction
needs tuning against real audio and latency measurements.
