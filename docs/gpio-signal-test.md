# GPIO Signal Test

`include/GpioSignalTest/` is a reusable GPIO waveform diagnostic component. A
configured instance either produces timer-driven PWM on one output pin or
consumes any-edge interrupts on one input pin. Pin, frequency, duty cycle,
report interval, pull mode, and measurement tolerances are configuration
values.

Consumers report the latest measured period, frequency, high and low times,
duty cycle, cumulative rising and falling edges, duplicate observations,
minimum/maximum period, period spread, edge age, and an `ok`/`bad` result. The
GPIO ISR only records atomic measurements; formatting and logging happen from
`work()`.

The v2 SPI0 bring-up profile is opt-in and disabled in the normal Master,
Media, and Power builds. Enabling `PUBSUB_STAR_SPI0_GPIO_SIGNAL_TEST=1` for all
three environments disables their normal SPI/application services. Master then
produces a distinct 50% duty waveform on every configured low-speed SPI net:

| Net | Master pin | Frequency |
| --- | --- | ---: |
| MOSI | GPIO3 | 7 Hz |
| MISO | GPIO1 | 11 Hz |
| SCLK | GPIO2 | 13 Hz |
| Media CS | GPIO4 | 17 Hz |
| Power CS | GPIO6 | 23 Hz |

Media consumes its configured MOSI/MISO/SCLK/CS pins and also the physically
connected GPIO4/GPIO2/GPIO3 jump pads. Power consumes its configured
MOSI/MISO/SCLK/CS pins. Each component also dumps the ESP-IDF GPIO matrix
configuration at startup.

Flash or start the Media and Power consumer images before the Master producer
image. This test intentionally reverses the normal MISO direction so Master can
fingerprint that net too. A normal SPI-slave image may drive MISO while its CS
waveform is low and must not run concurrently with the Master test producer.

An `observedMilliHz` value close to the net's table entry times 1000 identifies
the correct waveform. `result=bad` means the input is missing/stale, outside
the configured frequency or duty tolerance, has excessive cumulative period
spread, or produced duplicate edge observations.
