For your target, I’d start with microSD over SDMMC 1-bit.

It is the best balance:

- GPIO: 3 required signals: CLK, CMD, D0
- Throughput: already far beyond logging needs, and likely enough for occasional
    cached SPA fetches
- CPU: low, DMA-backed driver path
- Realtime impact: much better than LittleFS/SPI flash because it does not
    require instruction-flash cache shutdown
- Complexity: simpler than eMMC, easier parts/modules than eMMC, less bus
    contention than SDSPI

Tradeoffs:

| Option                          | Pins | Throughput |            CPU | Realtime Risk | Notes                                                                                            |
| ------------------------------- | ---: | ---------: | -------------: | ------------: | ------------------------------------------------------------------------------------------------ |
| SDMMC 1-bit microSD             |    3 |     plenty |            low |    low/medium | Best default. Add optional card-detect pin only if needed.                                       |
| SDMMC 4-bit microSD             |    6 |     higher | lower per byte |    low/medium | Overkill for logs; useful if serving larger assets often. More routing/pulls.                    |
| SDSPI microSD, separate SPI bus |    4 |         ok |         medium |        medium | Fine if SDMMC pins are awkward. Must not share PubSub SPI bus.                                   |
| SDSPI on PubSub SPI             |    4 |         ok |         medium |          high | Avoid. Storage stalls contend directly with realtime bus.                                        |
| eMMC SDMMC                      | 6-10 |       high |            low |    low/medium | Electrically robust, no socket/card issues, but parts/modules are annoying. DDR not needed here. |
| I2C FRAM                        |    2 |       tiny |     low/medium |           low | Excellent for last-error breadcrumbs, not SPA or bulk logs.                                      |
| external SPI NOR/FRAM           |    4 |     varies |         medium |        medium | OK only on separate SPI bus; NOR has erase/write latency and endurance concerns.                 |

“Low priority, no interrupts” is not quite attainable. Any sane block-storage
path will use DMA and interrupts. The important point is: storage interrupts
must not be on the critical bus/path, and storage must not invoke ESP32
instruction-flash cache shutdown. SDMMC satisfies that much better than
LittleFS.

For implementation, I’d design it like this:

- Master owns SD storage only.
- Nodes publish compact log records over PubSub.
- Master queues records into a bounded RAM buffer.
- A low-priority SD writer task batches writes, ideally 512-byte or larger
    chunks.
- Keep the log file open; rotate by size/time.
- Flush periodically and immediately for severe records, but never from PubSub
    callbacks.
- On queue pressure, drop/coalesce and increment metrics.

For the HTTP SPA: SDMMC 1-bit is probably enough. Use infinite cache, stream
chunks, and throttle if needed. If asset serving still causes measurable SPI
jitter, gate large HTTP reads behind a maintenance mode, but I would test SDMMC
1-bit before spending 6 pins on 4-bit.

