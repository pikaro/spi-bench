# Logic Board v2 GPIO Migration Analysis

Status: migration-wide investigation and owner discussion are complete. All
electrical inputs are resolved, the owner has authorized implementation, and
Media will retain its known-working 1 MHz SSD1306 overclock. No firmware change
had been made at the point this analysis was closed and converted into the
implementation plan.

Last updated: 2026-08-28

## Purpose

Record the evidence and decisions needed to migrate every logic-board firmware
GPIO assignment to the v2 schematic without losing investigation context. This
file is intentionally an analysis log, not an implementation plan.

The owner has required discussion of every strange or ambiguous electrical
detail before an implementation plan is created or executed.

## Scope calibration from owner feedback

The initial investigation went substantially deeper than requested in several
individual areas. The owner's correction is a depth calibration, not a request
to shorten or cancel the migration-wide investigation. The analysis must still
cover every affected node, shared protocol/build surface, host binding, test,
and relevant document before a plan is proposed. Discussion gates are limited to
findings that can plausibly cause an incorrect pin assignment, a failed
build/boot, contention during normal operation, invalid measurement data, or
make the requested architecture impossible. Reset-edge minutiae, component
bypassing, cable-hardening, byte packing, and optional memory
micro-optimizations remain recorded below as reference but are **not blockers**
unless bring-up produces concrete evidence of a problem.

The owner has already settled these implementation directions:

- disable the hardware UART on S3 nodes where GPIO43/GPIO44 are used by the
    schematic; and
- make the protocol ignore spurious Master attention inputs until the
    corresponding communication link is established;
- widen PubSub node IDs synchronously to 16 bits, move Host to the highest bit
    (`0x8000`), and give Power Host's former bit (`0x0080`);
- update libraries where needed for the S3 migration;
- keep the prototype Power node on ESP32-C3 and defer its future SD card to a
    bit-banged implementation;
- feed only the 24 V INA226 into `BatteryMonitor`, while the 5 V INA226 uses the
    ordinary INA behavior and local metrics;
- configure both INA226 modules for fitted 2 mOhm shunts, with U8 warning above
    2 A/error above 3 A and U9 warning above 500 mA/error above 1 A;
- power both INA logic supplies and their I2C pull-ups from `POWER_3V3` because
    Power owns the bus, while retaining `MASTER_3V3` for the RS485 HAT; and
- indicate the common post-U9 5 V rail with a green LED and 4.7 kOhm series
    resistor to ground; and
- enable PSRAM on every PSRAM-capable S3 node; the C3-based IO and Power nodes
    have no PSRAM to enable;
- treat deletion of the agent-only pin-connectivity contract as intentional and
    do not restore it; and
- add four 10 kOhm CS pull-ups from the slave-side CS nets to `MASTER_3V3`, and
    a 100 nF decoupling capacitor directly across U6's 5 V/GND supply; and
- retain Media's 1 MHz SSD1306 rate as an intentional, measured v1 overclock,
    while disabling the MCU's redundant internal I2C pull-ups; and
- treat installed USB connections as data-only, leave the C3 disconnected from
    USB in situ, and never select/use USB power while external power is
    connected.

The owner also permits small quality improvements in code already touched by
the migration. These remain bounded to changes that support the migration or
make its new contracts explicit; unrelated cleanup is out of scope.

The approved 16-bit node-ID change can remain approximate: one extra byte per
PubSub frame, negligible 32-bit-MCU compute cost, a few hundred bytes on a
simple node, and roughly 1 KiB on Master after Power becomes the second
low-speed peer. No queue-layout or nine-slot micro-optimization is warranted
unless post-change firmware maps show an actual constraint.

## Migration coverage ledger

This is an investigation checklist, not an implementation plan. It exists to
prevent confidence in one solved area from hiding unfinished migration work.

| Surface                              | Current finding                                                                                                                                                                                  | Investigation status                                      |
| ------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------- |
| Schematic and netlist                | All five node pin maps cross-checked against source and both netlists; revised INA/SW1 path regenerated exactly; agent-only connectivity contract deletion is intentional                       | Complete                                                  |
| Shared node identity and wire format | Direct synchronized 16-bit widening approved; Power is `0x0080`, Host is `0x8000`; firmware, generated bindings, and native host peer surfaces identified                                      | Complete enough to plan                                   |
| Master                               | Existing v2 GPIOs match; Power requires a second Bus3 device, clock handler, and a two-peer low-speed SPI router                                                                                 | Complete enough to plan                                   |
| Power                                | Requires Core, SPI edge, clock sync, I2C master, U8 BatteryMonitor path, and U9 ordinary metrics path; addresses, shunts, current ranges, rail ownership, and switched-load topology are settled | Complete enough to plan                                   |
| Media                                | All legacy GPIO replacements known; S3 target conflict and audio-tools v1.2.2 failure reproduced; active-low GPIO44 LED support, UART displacement, S3 PSRAM, and retained 1 MHz I2C are settled | Complete enough to plan                                   |
| GPU0/GPU1                            | Existing worktree GPIO/SK9822/output-gate changes match v2 and both targets build                                                                                                                | Preserve and regress only                                 |
| UART and attention safety            | Master and Media must use USB Serial/JTAG as primary console; SPI and RS485 Master attention must ignore pre-link edges/levels                                                                   | Complete enough to plan                                   |
| Build/component configuration        | Media must inherit S3 CMake features and audio-tools v1.2.5; Power must enable SPI and I2C                                                                                                       | Complete enough to plan                                   |
| Tests and generated artifacts        | All wire-format consumers require synchronized regeneration; active firmware builds and targeted protocol/bring-up validation identified                                                         | Complete enough to plan                                   |
| Documentation                        | Legacy pin, Power-node, address, capacity, console, topology, and stack-tooling drift identified                                                                                                 | Complete enough to plan                                   |

## Scope

The v2 logic-board schematic contains these firmware nodes:

- `master`: ESP32-S3 Zero (`U1`)
- `media`: ESP32-S3 Zero (`U2`)
- `gpu0`: ESP32-S3 Zero (`U3`)
- `gpu1`: ESP32-S3 Zero (`U4`)
- `power`: ESP32-C3 SuperMini (`U5`)

The external `io`, standalone `ai`, `scratch`, and `wheel` targets are not
represented in this schematic. Their board-local GPIOs are therefore outside the
schematic-derived mapping unless a separate source of truth is provided.

## Evidence and confidence

Pin mapping is supported by three mutually consistent current repository
artifacts:

1. `schematic/perfboard-v2.py`, the SKiDL electrical source.
2. `schematic/build/perfboard-v2.config-netlist.json`, the deterministic
    logical-signal-to-node-pin mapping generated specifically for firmware.
3. `schematic/build/perfboard-v2.net`, the standard KiCad netlist.

The schematic source was regenerated to a temporary directory after the
U8/U9/SW1 edit. The regenerated configuration netlist is byte-identical to the
checked-in generated artifact; the standard KiCad netlist differs only in its
generation timestamp and temporary source path. SKiDL reported zero ERC errors.
KiCad reported zero errors and 91 warnings, all from the known absent
footprint/symbol-library context of the generated schematic.

The former fourth artifact, `schematic/lib/connectivity_contract.py`, is deleted
in the current worktree and its call was removed from
`schematic/lib/kicad_output.py`. It was an agent-only exact snapshot rather than
a maintainable project contract, and the owner confirmed its deletion is
intentional. The migration must not restore it. Regenerated netlists, ERC, and
targeted review of changed electrical paths remain the validation surface.

The KiCad schematic analyzer completed but reported zero datasheet-backed
findings because the custom modules have no MPN metadata and the project has no
`datasheets/` directory. Its pin/electrical results are consistency evidence,
not manufacturer-datasheet verification. Several voltage-domain and missing
pull-up findings are known analyzer false positives caused by the custom module
abstraction and split series-resistor nets. The apparent duplicate-INA226
address is resolved by the fitted straps described below.

A repository-wide GPIO-use search found production pin assignments only in the
four existing node configs (`master`, `media`, `gpu`, and board-external `io`),
plus the direct Master present-strobe output. The current Master/GPU assignments
match v2, every mismatching in-scope constant is confined to legacy
`src/media/config.hpp`, and Power has no peripheral constants yet. Generic
platform pin enums and the board-external IO/AI/scratch roles are definitions or
different hardware, not additional v2 mappings. This closes the hidden-GPIO part
of the migration audit.

## Schematic-derived pin map

### Master (`U1`, ESP32-S3 Zero)

| Function                 |     Module pin | Physical GPIO |
| ------------------------ | -------------: | ------------: |
| Low-speed SPI MISO       |        `GPIO1` |             1 |
| Low-speed SPI clock      |        `GPIO2` |             2 |
| Low-speed SPI MOSI       |        `GPIO3` |             3 |
| Media chip select        |        `GPIO4` |             4 |
| Media attention          |        `GPIO5` |             5 |
| Power chip select        |        `GPIO6` |             6 |
| GPU0 attention           |        `GPIO7` |             7 |
| GPU0 chip select         |        `GPIO8` |             8 |
| Power attention          |        `GPIO9` |             9 |
| LED present strobe       |       `GPIO10` |            10 |
| High-speed SPI MOSI      |       `GPIO11` |            11 |
| High-speed SPI clock     |       `GPIO12` |            12 |
| High-speed SPI MISO      |       `GPIO13` |            13 |
| RS485 RX                 |       `GPIO14` |            14 |
| RS485 TX                 |       `GPIO15` |            15 |
| RS485 attention          |       `GPIO16` |            16 |
| GPU1 chip select         |           `RX` |            44 |
| GPU1 attention           |           `TX` |            43 |
| On-module RGB status LED | internal alias |            21 |

The current modified `src/master/config.hpp` matches every listed active Master
GPIO except that no second logical low-speed SPI master link is yet configured
for Power GPIO6/GPIO9. The existing low-speed PubSub transport is point-to-point
to Media.

The current Master baseline builds successfully with these pre-existing mappings
(195,960 bytes reported RAM use and 1,099,475 bytes flash use).

The current `sdkconfig.stack.master` comment and PSRAM disable still refer to
the obsolete GPIO36/GPIO37 low-speed bus. That rationale no longer matches v2;
Master PSRAM should now be enabled along with the other S3 nodes.

### Media (`U2`, ESP32-S3 Zero)

| Function                  |     Module pin | Physical GPIO |
| ------------------------- | -------------: | ------------: |
| I2S word select / LRCK    |        `GPIO1` |             1 |
| Display I2C SCL           |        `GPIO5` |             5 |
| Display I2C SDA           |        `GPIO6` |             6 |
| SPI attention             |        `GPIO7` |             7 |
| SPI chip select           |        `GPIO8` |             8 |
| SPI MOSI input            |        `GPIO9` |             9 |
| SPI clock input           |       `GPIO10` |            10 |
| SPI MISO output           |       `GPIO11` |            11 |
| I2S microphone data input |       `GPIO12` |            12 |
| I2S bit clock             |       `GPIO13` |            13 |
| Beat indicator drive      |           `RX` |            44 |
| On-module RGB status LED  | internal alias |            21 |

The current Media configuration is entirely legacy ESP32-original wiring:

- beat indicator: GPIO26 rather than RX/GPIO44
- display I2C: SDA22/SCL21 rather than SDA6/SCL5
- I2S: BCLK25/WS32/DIN33 rather than BCLK13/WS1/DIN12
- SPI: original-ESP32 `VSPI_*` aliases rather than GPIO9/11/10 and CS8
- attention: GPIO17 rather than GPIO7
- status LED: GPIO16 rather than the S3-Zero GPIO21 status-LED alias

`platformio.ini` declares `env:media` as extending `esp32s3zero`, but its
component feature arguments inherit from `esp32orig` and its flags explicitly
define `CONFIG_IDF_TARGET_ESP32=1`. This is inconsistent with an ESP32-S3
target. The environment currently also inherits PSRAM-disabled CMake arguments
from the original ESP32 stack despite the S3-Zero board definition. An older
Media compile database confirms that the environment previously built with the
original ESP32 compiler, board macro, and include tree, but it is stale relative
to the current `extends = esp32s3zero` declaration.

A fresh `bin/build -e media` establishes the present behavior. PlatformIO now
selects `custom_esp32s3_zero`, the Xtensa ESP32-S3 toolchain, S3 include trees,
and `CONFIG_IDF_TARGET_ESP32S3=y`. The build nevertheless fails because the
manual `-DCONFIG_IDF_TARGET_ESP32=1` selects original-ESP32 conditional code at
the same time. The SARIF diagnostics include original-ESP32 eFuse and SPI-flash
symbols being requested from S3 headers, plus the expected legacy Media GPIOs
being absent from the S3-Zero `Pin` enum. The audio-tools dependency also sees
conflicting ESP32/S3 platform macros and reports target-specific timer/I2S
errors. The forced target macro is therefore build-breaking and must be removed;
this is no longer a hypothetical cleanup.

The inherited original-ESP32 CMake feature set separately supplies
`ENABLE_PSRAM=OFF`, while the generated S3 `sdkconfig.media` requests
`CONFIG_SPIRAM=y`. This is an internally inconsistent component/configuration
pair rather than a trustworthy Media setup. The migration should inherit the
S3-Zero CMake feature set so the requested PSRAM component is present.

After isolating those flag/component problems in a temporary configuration, the
pinned audio-tools v1.2.2 still fails independently: it defines `USE_TIMER` for
ESP32-S3 under ESP-IDF, but gates its ESP32 timer-driver type behind `ARDUINO`,
leaving `TimerAlarmRepeatingDriver` undefined. This is an upstream dependency
defect, not a GPIO error. The official v1.2.5 release explicitly lists a
correction for the broken `AudioTimerESP32.h`:

<https://github.com/pschatzmann/arduino-audio-tools/releases/tag/v1.2.5>

An isolated build against the exact upstream v1.2.5 tag removed that timer
failure. With the corrected S3 target flags, S3 PSRAM component, and v1.2.5, the
only remaining compile errors were the known legacy Media pin constants; the
target framework and audio dependency otherwise compiled. Updating the pinned
dependency is therefore the approved clean direction, subject to normal
compile/runtime regression checks. The manual `-DESP32` flag is also redundant
because the common `ESP32_CMAKE` path defines the audio library's ESP32 family
macro; its removal avoids a macro-redefinition warning.

The repository also retains a fully commented `env:media-btstack` block which
explicitly extends the original ESP32 board and a tracked
`sdkconfig.stack.media-btstack` overlay enabling Bluetooth Classic/BR-EDR. The
installed S3 capability headers expose BLE but not `SOC_BT_CLASSIC_SUPPORTED`,
so that internal A2DP experiment is not a valid v2 Media alternative. Given the
stated nRF54L15 sidecar direction, the clean v2 scope is to retire that dead
original-board environment/overlay and describe the reusable A2DP source
implementations as legacy diagnostics, without removing the shared audio-source
code or inventing the future sidecar protocol.

### GPU0 (`U3`, ESP32-S3 Zero)

| Function        | Module pin / GPIO |
| --------------- | ----------------: |
| SPI chip select |                 1 |
| SPI attention   |                 2 |
| LED clock       |                 7 |
| LED data        |                 8 |
| Present strobe  |                10 |
| SPI MOSI input  |                11 |
| SPI clock input |                12 |
| SPI MISO output |                13 |

### GPU1 (`U4`, ESP32-S3 Zero)

| Function                            | Module pin / GPIO |
| ----------------------------------- | ----------------: |
| SPI MISO output                     |                 1 |
| SPI clock input                     |                 2 |
| SPI MOSI input                      |                 3 |
| Present strobe                      |                 4 |
| SPI chip select                     |                 5 |
| SPI attention                       |                 6 |
| Active-low shared LED output enable |                 9 |
| LED clock                           |                10 |
| LED data                            |                13 |

The current modified GPU configuration matches the schematic for SPI, attention,
chip selects, present strobes, SK9822 clock/data, and the GPU1 active-low
74AHCT125 enable. These are pre-existing worktree changes and must not be
overwritten.

Both current GPU environments build successfully. This verifies the existing
v2/SK9822 source state at compile time only; it does not verify signal polarity,
reset behavior, or the physical LED output gate.

### Power (`U5`, ESP32-C3 SuperMini)

| Function        | Module pin / GPIO |
| --------------- | ----------------: |
| SPI chip select |                 0 |
| SPI MOSI input  |                 1 |
| SPI clock input |                 3 |
| SPI MISO output |                 4 |
| INA226 I2C SDA  |                 7 |
| INA226 I2C SCL  |                 8 |
| SPI attention   |                10 |

`src/power` currently starts only `CoreSetup`; it has no SPI or I2C objects or
configuration. `env:power` exists but does not enable the SPI or I2C ESP-IDF
components.

### Power software topology and behavior

The current no-peripheral Power baseline builds successfully for the C3
SuperMini (66,532 bytes reported RAM use and 576,616 bytes flash use). The
existing reusable pieces support the requested topology without a new driver:

- one C3 `Wire::Spi::Slave` on Bus2, GPIO1 MOSI, GPIO4 MISO, GPIO3 SCLK, GPIO0
    CS, and GPIO10 attention;
- one `Wire::I2C::Master` on Bus0, GPIO7 SDA and strapping GPIO8 SCL, with
    external pull-ups represented by `enableInternalPullups = false`;
- two model-selected `Ina2xx` instances sharing that master, each configured as
    INA226 at a distinct address; and
- the same clock-slave/clock-sync and PubSub SPI-edge composition used by Media
    and the GPU nodes, if Power is intended to be a routed node.

Power's status LED should remain unconfigured: the C3 SuperMini hardware model
aliases its onboard LED to GPIO8, which v2 consumes as SCL. Enabling the normal
status service there would directly corrupt I2C traffic.

On Master, adding Power is more than another CS constant. The low-speed bus
currently owns one `Wire::Spi::Master` link to Media and a point-to-point
`SpiTransport`. The platform bus wrapper already supports several devices on one
physical SPI host (as proven by the existing two-link GPU bus), but v2 needs a
second low-speed Master link plus a two-peer `SpiRouterTransport` for Media and
Power. The Master clock must register both physical links. This is a contained
shared setup change using the approved Power ID `0x0080`.

There is currently no production Power payload or publisher. `Topic::Power`
exists, but it is used only by transport tests. INA226 metrics are registered in
the local metrics backend; Core does not automatically send that metrics store
over PubSub. Thus adding SPI, PubSub, and two INA instances would make the node
reachable and locally observable, but would not by itself deliver voltage,
current, or power readings to Master.

The owner confirmed the existing BatteryMonitor boundary: U8 is the battery
source and alone feeds `BatteryMonitor`; U9 is the informational 5 V rail and
uses only the ordinary INA sampling/metrics behavior. No new Power publication
payload has been requested, so the migration should not invent one. Power should
also register the existing optional BatteryMonitor command adapter so
`/battery status` and later calibration commands remain available over its
normal console; this does not create network telemetry.

The owner corrected the U9 topology in the schematic. The regenerated path is
now:

`LM2596 OUT+ -> SW1 throw 1`, `VUSB_5V -> SW1 throw 3`, then
`SW1 common 2 -> U9 C+ -> 2 mOhm shunt -> U9 C- -> common VCC_5V`.

U9 therefore measures total common-rail load current from whichever source SW1
selects. Its `V+` voltage-sense terminal is connected immediately after SW1 on
the C+ side and `V-` is grounded, so it reports selected-source voltage just
upstream of the shunt. The maximum stated U9 load drops only 2 mV across the 2
mOhm shunt, so this placement does not create a meaningful distinction from
downstream rail voltage. Normal current direction is C+ to C-, producing a
positive reading.

U8 follows the same terminal convention:
`VIN_24V -> U8 C+ -> 2 mOhm shunt -> U8 C- -> buck/J6`, with `V+` on `VIN_24V`
and `V-` grounded. The owner confirms that C+/C- are the module's high-current
terminals on opposite sides of the fitted shunt. IN+/IN- are the internal
Kelvin/sense nodes reached from the shunt through 4.7 Ohm series resistors, so
leaving those module pins externally unconnected is intentional. The 4.7 Ohm
input resistors also fall within TI's recommended maximum 10 Ohm per input for
the optional INA226 differential-input filter/protection network.

For this prototype, the existing scratch fail-fast policy is the pragmatic
default: failure to identify or configure either required INA226 aborts Power
startup instead of presenting a partially valid monitoring node. Graceful
single-sensor degradation can be added later when remote fault reporting has a
defined consumer. This is a behavioral assumption to review, not an electrical
blocker.

No INA226 ALERT pins are wired in v2, so both devices must use polling and leave
their optional hardware-alert configuration unset.

### Resource feasibility of the completed topology

The architecture fits comfortably enough that it does not require redesign.
Target-ABI object probes include each component's static task storage:

- one current S3 SPI Master link is 18,544 bytes;
- changing Master's low-speed PubSub transport from the 6,040-byte
    point-to-point form to the existing 7,432-byte two-peer router form adds
    about 1.4 KiB; and
- the accepted 16-bit identity widening is expected to add roughly another 1 KiB
    to the complete Master topology.

The second physical low-speed link therefore adds about 20--21 KiB in total, not
merely the one-byte wire-ID overhead. Against the current 195,960-byte Master
RAM report, the resulting static use should remain around 217 KiB, roughly two
thirds of the PlatformIO 327,680-byte budget. The final map must be recorded,
but there is no present capacity blocker.

Master's static task registry allows ten sources. The present production
composition registers eight managed tasks; the additional Power SPI Master task
raises that to nine, leaving one slot. Power's proposed common/SPI/PubSub tasks
also remain below the same limit. Proposed task names fit the registry's
16-character source-name bound.

On an edge node, the current SPI Slave object is 18,624 bytes and the complete
SPI PubSub edge setup is 27,568 bytes. Adding those plus BatteryMonitor
(currently 3,640 bytes), two roughly 696-byte INA instances, and small I2C/clock
objects to Power's 66,532-byte baseline gives a coarse total near 120 KiB before
ordinary link/layout variance. That remains well inside the C3 budget. These are
estimates from current S3/C3-compatible layouts, not a substitute for the final
Power map.

### Critical future constraint: Power has no independent hardware SD interface

The stated future Power role includes an SD card, but the selected ESP32-C3 and
the v2 SPI topology do not have an unused hardware controller for it. Espressif
documents SPI0 and SPI1 as flash controllers and SPI2 as the only
general-purpose SPI controller, capable of master **or** slave operation:

<https://documentation.espressif.com/esp32-c3_datasheet_en.html>

The installed ESP-IDF capability table agrees (`SOC_SPI_PERIPH_NUM == 2`, where
the exposed hosts are flash SPI1 and general-purpose SPI2), and the repository's
SPI platform deliberately rejects `Bus3` on C3. The v2 Power-to-Master link
already requires SPI2 continuously in slave mode. The C3 also has no hardware
SDMMC host capability.

Four nominally spare GPIOs do not solve the controller conflict. The viable
classes of solution are materially different:

1. use software/bit-banged SDSPI on spare pins, accepting a new driver plus
    throughput, CPU-load, timing, and validation costs;
2. time-multiplex SPI2 between slave and SD-master modes, taking the Power node
    off the live PubSub link during storage access (unlikely to fit a
    continuous monitoring role);
3. move storage to a different node or storage coprocessor; or
4. change the Power MCU to one with a second general-purpose SPI controller or
    an SDMMC host.

There is no existing software-SPI SD implementation in the repository. The
existing `docs/sdmmc.md` recommendation is for storage on Master and assumes an
SDMMC-capable target, so it does not apply to the C3 Power node. The intended
prototype direction is now settled: keep the C3 and use bit-banged SD later,
accepting the expected low throughput. That later implementation is outside the
present migration, but spare pins must not be consumed accidentally.

## Investigated electrical concern: shared SPI MISO

Both SPI buses share MISO between two slaves: Media plus Power on the low-speed
bus, and GPU0 plus GPU1 on the high-speed bus. This initially appeared critical
because Espressif documents that the original ESP32 continues to drive MISO when
CS is inactive, in which case the single master-side 33 Ohm resistor would not
prevent direct contention between the two slave outputs.

That restriction does **not** apply to the actual v2 targets. Espressif's
canonical SPI-slave documentation places the warning inside an `esp32`-only
conditional, while the S3 and C3 documentation explicitly describes each device
as active only while its individual CS is asserted:

- <https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/peripherals/spi_slave.rst>
- <https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/spi_slave.html>
- <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c3/api-reference/peripherals/spi_slave.html>

The locally installed ESP-IDF 5.5.3 source also confines its original-ESP32
MOSI/MISO slave workaround to `CONFIG_IDF_TARGET_ESP32`. On current evidence,
the shared-MISO topology is valid for S3/C3 and is not an implementation
blocker. Because unintended contention is high consequence, bring-up should
still verify that each unselected slave releases MISO with a scope or logic
analyzer before sustained traffic.

The outer SPI `SlotHeader.peerId` was traced separately during the 16-bit-ID
audit. Although it is eight bits wide, the current physical SPI transceivers
initialize it to zero and the PubSub router does not copy its one-hot routed
peer ID into that field. It is link-local/reserved state, not the serialized
PubSub source ID, and does not need to widen merely to represent Power. A test
should preserve that decoupling so a future refactor cannot silently truncate
Power's `0x0080` routed ID into the outer slot.

## Electrical bring-up notes and remaining configuration inputs

### Recommended: add 10 kOhm pull-ups to all active-low SPI chip selects

None of the four slave-select nets (`CS_MEDIA`, `CS_POWER`, `CS_GPU0`, or
`CS_GPU1`) currently has an external pull-up. Espressif's S3 pin table shows
Master GPIO4, GPIO6, and GPIO8 in an un-biased/high-impedance reset state; only
the GPIO44/U0RXD GPU1 select has a default weak pull-up. The slave-side S3
GPIO1, GPIO5, and GPIO8 and C3 GPIO0 likewise do not provide a guaranteed
inactive level for every pairing. The official hardware-design guidance
recommends biasing high-impedance pins when an external circuit requires a
defined state:

- <https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf>
- <https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf>
- <https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html>

Shared MISO is safe only while every unselected slave actually sees its CS
inactive. During staggered boot, reset, or independent power-up, a slave can
initialize its SPI peripheral before Master has driven its floating CS high. Two
selected slaves could then drive the directly joined MISO net; the single 33 Ohm
resistor is on the common Master branch and does not isolate one slave from the
other.

No slave CS pin is a boot strap: Media uses S3 GPIO8, Power uses C3 GPIO0, GPU0
uses S3 GPIO1, and GPU1 uses S3 GPIO5. A 10 kOhm pull-up therefore does not
change boot selection. Once Master actively drives a CS line, the pull-up has no
effect on SPI timing. When selected low it adds only 0.33 mA at 3.3 V; the
existing 33 Ohm series resistor then adds about 11 mV of drop. This is
electrically inconsequential for the GPIO driver while materially improving the
reset and staggered-start state.

The approved schematic change is one 10 kOhm resistor from each slave-side CS
net to `MASTER_3V3`:

| Slave | Pull-up net       | Rail         |
| ----- | ----------------- | ------------ |
| Media | `SPI0_CS_MEDIA`   | `MASTER_3V3` |
| Power | `SPI0_CS_POWER`   | `MASTER_3V3` |
| GPU0  | `SPI1_CS_GPU0`    | `MASTER_3V3` |
| GPU1  | `SPI1_CS_GPU1`    | `MASTER_3V3` |

The owner explicitly assigns pull-up ownership to Master. GPU0's 3.3 V module
output remains no-connect. Each resistor is placed after its 33 Ohm series part
so the slave input receives the defined inactive level. Firmware should still
assert Master CS outputs high early, enable slave-side internal pull-ups before
SPI initialization, and reject attention or data before link establishment as
defense in depth. Scoped bring-up should confirm all unselected slaves release
MISO.

### Settled operating constraint: module USB VBUS rails are tied together

All four Waveshare ESP32-S3-Zero `5V` pins and the Power C3 SuperMini `5V` pin
are connected directly to the schematic's common `VCC_5V` rail. The official
Waveshare ESP32-S3-Zero schematic shows its USB Type-C `VBUS` net feeding both
the onboard regulator and the castellated `5V`/VBUS pad without an isolation
diode:

<https://files.waveshare.com/wiki/ESP32-S3-Zero/ESP32-S3-Zero-Sch.pdf>

This has three consequences which firmware cannot prevent:

1. Connecting one S3-Zero USB cable can energize the complete common 5 V rail
    and every node, not just the connected module.
2. Connecting multiple USB cables ties their host VBUS supplies together.
3. When SW1 selects the buck path, module USB VBUS can feed `VCC_5V` backward
    through the closed switch, the 5 V INA226 path/shunt module, and toward the
    LM2596 output.

The exact C3 SuperMini board schematic/revision is not recorded, but no C3
behavior is required for the first two risks because the four S3-Zero modules
already share their VBUS pads.

The installed wiring avoids those source-paralleling cases operationally:

- S3 Zeros use data-only downstream cables through the hub IC, with host VBUS
    not carried to the modules;
- the hub daughterboard separately breaks out USB VCC and GND to its onboard USB
    connector;
- the C3 is not connected by USB in situ; and
- USB power is never selected while external power is connected.

This is a valid operating constraint rather than a firmware requirement. It must
be stated in bring-up/calibration documentation because an ordinary
VBUS-connected bench cable would bypass it. Under the stated in-situ wiring, the
earlier multiple-host-VBUS and buck-backfeed concerns are resolved.

### INA226 addresses and calibration

Both `U8` (24 V rail) and `U9` (5 V rail) are represented as INA226 modules on
the same SDA/SCL pair. The owner reports A1 tied to ground on both devices, with
U8 A0 tied to ground and U9 A0 tied to VCC. TI's address table confirms these as
7-bit addresses `0x40` and `0x41`, respectively:

<https://www.ti.com/lit/ds/symlink/ina226.pdf>

The driver requires, separately for each live INA226 instance:

- 7-bit I2C address
- shunt resistance in micro-ohms
- expected maximum current for calibration
- practical and absolute voltage/current windows
- a unique metrics group name

The address ambiguity and calibration inputs are resolved. Both fitted shunts
are 2 mOhm. U8 has a normal target near 1 A, a practical-high warning at 2 A,
and an absolute-high error at 3 A. U9 has a practical-high warning at 500 mA and
an absolute-high error at 1 A. The owner's battery error bounds are
approximately 20 V low and 30 V high for the 7S4P pack, with capacity
provisionally around 6 Ah until calibration. Existing `BatteryMonitor` defaults
currently encode 21.0 V practical minimum, 17.5 V absolute minimum, 29.4 V
practical maximum, 30.1 V absolute maximum, and 10 Ah. The production mapping is
settled below, including the strict-boundary detail.

The revised current path is well inside the INA226 input range. TI specifies a
shunt-voltage range of approximately +/-81.92 mV and a 2.5 uV shunt-voltage LSB.
At 2 mOhm, U8 produces 6 mV and dissipates 18 mW in the shunt at 3 A; U9
produces 2 mV and dissipates 2 mW at 1 A. These limits do not approach device
shunt-input saturation. The INA226's specified +/-10 uV maximum input offset at
25 degrees C corresponds to about +/-5 mA at 2 mOhm, before module/shunt error,
so the existing 5 mA BatteryMonitor deadband is at the edge of that zero-current
offset. This affects only near-zero charge/discharge classification, not the
stated operating-current range.

The repository driver also accepts both configurations cleanly:

- U8 with expected maximum 3,000,000 uA selects 92 uA/current-register LSB,
    calibration register 27,826, and positive full scale 3,014,564 uA.
- U9 with expected maximum 1,000,000 uA selects 79 uA/current-register LSB,
    calibration register 32,405, and positive full scale 2,588,593 uA. The
    apparently wider range is intentional: with a 2 mOhm shunt, the INA226's
    15-bit calibration-register maximum sets a 79 uA minimum current LSB.

No driver change or inflated U9 expected-current value is required. Shunt
tolerance and assembled-module routing remain empirical accuracy inputs, so
bring-up must still compare both sensors against a meter/known load before their
current and power values are trusted.

### ESP32-C3 GPIO8 used as Power I2C SCL

The schematic assigns Power SCL to GPIO8. In the repository hardware model,
GPIO8 is both `StrappingGPIO8` and `PwmLed`, reflecting the common C3 SuperMini
onboard LED connection and the ESP32-C3 boot-strapping role. The schematic also
adds a 10 kOhm external pull-up to `POWER_3V3`.

This is usable, but it means the clock line shares the onboard LED circuit and
influences a strapping pin during reset. Firmware should use it as drawn and
leave the normal Power status-LED service disabled.

Manufacturer evidence narrows the boot risk: Espressif specifies GPIO2, GPIO8,
and GPIO9 as C3 strapping pins, but normal SPI boot requires GPIO9 high and
accepts either GPIO8 value. Thus the 10 kOhm SCL pull-up on GPIO8 does not by
itself select the wrong boot mode. GPIO8 still controls ROM-print behavior for
some eFuse states, and the SuperMini onboard-LED implementation may add load to
SCL. This is a bring-up check if the 100 kHz I2C bus has bad edges or fails to
enumerate, not a reason to block the drawn pin assignment.

### ESP32-S3 GPIO3 strapping and power-up behavior

Master low-speed MOSI and GPU1 SPI MOSI use S3 GPIO3. Espressif documents GPIO3
as the JTAG-source strapping pin, not a boot-mode pin. With default unburned
eFuses its level is ignored; if `EFUSE_STRAP_JTAG_SEL` has been burned, GPIO3
selects USB versus pad JTAG. This should not stop normal firmware boot, but the
firmware/hardware assumptions require that none of these modules have a custom
JTAG-selection eFuse state.

Espressif also documents approximately 60 microsecond low-output power-up
glitches on S3 GPIO1 through GPIO14. Most v2 SPI, I2C, I2S, strobe, and LED
signals occupy that range. At simultaneous cold power-up the Master is not yet
transacting, but hot-plugging or separately powering an S3 module while the
shared buses are live could briefly drive an otherwise input signal low and
contend with an active peer. The existing 33 Ohm series resistors provide some
limiting on SPI lines but do not make arbitrary hot-plugging safe. Normal
operation is assumed to use common-rail power-up, with live module insertion
unsupported; reset with rails continuously powered is a distinct and less severe
case than power-up.

### Settled: S3 UART0 console overlaps Master GPU1 signals

The generated Master and Media SDK configurations currently select a custom
primary UART0 console at 921600 baud on the ESP32-S3 default pins:

- TX: GPIO43
- RX: GPIO44

USB Serial/JTAG is enabled only as a secondary console. The repository console
writes through `stdout`, and the installed ESP-IDF implementation sends console
characters to the primary ROM console and mirrors them to the configured
secondary USB console. Therefore this is a real pin assignment, not merely a
stale comment.

On Master, the schematic simultaneously assigns GPIO43/TX to GPU1 attention
through R15 (1 kOhm) and GPIO44/RX to GPU1 CS through R14 (33 Ohm). Startup and
application log output can consequently pulse the GPU1 attention line until the
SPI setup remaps GPIO43 as an input. The GPU attention output is open-drain, so
an opposing GPU low is current-limited by R15, but the unintended signaling is
still wrong. GPIO44 begins as a UART input, so its overlap is less severe until
firmware turns it into CS.

Media also uses GPIO44 for the active-low beat LED. Its console overlap begins
as UART RX/input and is therefore not the same drive-contention case, but the
UART matrix still has to be displaced when the LED output is initialized.

The owner directed that the hardware UART be disabled on nodes where these pins
are repurposed. The clean ESP-IDF configuration is to make USB Serial/JTAG the
primary console, not merely a secondary mirror, in the Master and Media
sdkconfig overlays. This removes application/bootloader UART ownership while
preserving interactive console input and output over USB Serial/JTAG. ROM-edge
behavior before the configured bootloader runs is a bring-up observation rather
than a planning blocker. Power uses the C3 default UART pins GPIO21/GPIO20,
which the v2 schematic does not otherwise assign.

The existing SPI Master attention path does not yet meet the other half of the
owner's requirement. `_onAttentionLine()` records and wakes on every asserted
edge, while `_consumeAttentionRequest()` also samples a held-low line, even
before the transceiver is ready. A check only inside `_runTurn()` would be too
late because the wake itself can provoke a handshake transfer. The contained
contract change is an explicit atomic attention-ready gate which is false at
begin/reset/end, becomes true when the SPI handshake completes, and is checked
both before ISR wakeup and before level sampling. Periodic handshake retries
remain independent, and a line still held low after readiness will be consumed
on the next normal task step.

The RS485 Master path has the same contract issue in a less visible form. Before
sync, its task performs only handshake work, but the shared RS485 node still
records/wakes on attention and can carry that stale request into the first ready
transaction step. Its existing atomic `NodeState` already provides the gate:
Master-side attention ISR/notify handling and held-low sampling should return
without recording while `ready()` is false, with reset clearing any pending
request as it already does. RS485 hello retry and UART-data wakeups are
independent of attention, so this cannot deadlock establishment. The slave side
continues to drive attention normally once application sends are permitted.

### GPU1 LED-output enable during power-up

All four 74AHCT125 active-low output enables are tied to GPU1 GPIO9 and pulled
up to `GPU1_3V3`, while the buffer itself is powered from the common 5 V rail.
This is valid at steady state because AHCT inputs use TTL-compatible thresholds,
but the disable pull-up is not established until the GPU1 3.3 V rail rises.
Espressif also documents the approximately 60 microsecond low-output power-up
glitch on S3 GPIO9. That low enables all four buffers while the LED clock/data
GPIOs can be in their own power-up transition.

The likely consequence is brief clock/data garbage or a startup flash on the
SK9822 outputs, not MCU-to-MCU bus contention. `beginDisabled` in firmware runs
too late to prevent the silicon power-up interval. This is a hardware follow-up
only if an unacceptable startup flash is observed; a hardware-safe correction
would reference the enable arrangement to the 5 V buffer domain, and the ESP32
GPIO itself must not be pulled to 5 V.

### 74AHCT125 has no schematic decoupling capacitor

`U6` is a bare 5 V logic IC rather than a module, but the v2 schematic contains
no bypass capacitor across its VCC/GND pins. Four fast AHCT outputs can switch
simultaneously into external LED wiring. A local 100 nF ceramic at pins 14/7 is
a recommended schematic/hardware correction if it is not already fitted, but it
does not alter the GPIO migration; firmware cannot compensate for local supply
bounce if signal integrity later proves poor.

### RS485 HAT identity and single-ended attention line

`U10` is a custom `Module_Hat_RS485` abstraction with no manufacturer or part
number. Firmware assumes a transparent UART-to-RS485 module: only RX and TX are
wired, while its `S` pin is intentionally unconnected. Its VCC is supplied by
the Master module's 3.3 V output. Without the exact module/revision, it is not
possible to verify 3.3 V supply compatibility, automatic direction control, the
meaning of `S`, or onboard termination/failsafe biasing. This interface and its
current firmware are unchanged by v2, so the migration should preserve them
rather than speculate about a different HAT contract.

In addition, J2 carries `RS485_ATTN` as an ordinary single-ended 3.3 V
open-drain GPIO alongside the differential pair. It has a 1 kOhm series resistor
but no schematic pull-up, ESD device, or differential protection; the Master
currently relies on its internal weak pull-up. Cable hardening is a separate
deployment concern, not a GPIO-v2 plan gate.

### Settled: controller-owned INA 3.3 V domain

The INA226 modules' `VCC` pins, their I2C pull-ups, and the I2C controller are
all powered from the Power MCU's `POWER_3V3` rail. This lets the Power node and
both monitors start together when the Master module is absent and avoids
energizing the INA I2C pins while their logic supply is off. The RS485 HAT
remains powered from `MASTER_3V3`. Both module regulators are fed from the
common selected 5 V rail.

The Media beat LED is also powered from `MASTER_3V3` but driven by the Media
MCU. Its anode is tied to `MASTER_3V3`; its cathode reaches Media RX/GPIO44
through R19, so it is electrically active-low. Current LedPwm code assumes
active-high duty and has no per-output inversion setting.

The schematic makes the Media LED's active-low behavior unambiguous. The
installed ESP-IDF LEDC driver supports hardware GPIO output inversion per
channel. The minimal software surface is therefore a default-false inversion
option in the shared LedPwm platform configuration, enabled only by Media; PWM
brightness and animation semantics remain unchanged, and zero duty remains
physically off. This is a shared-code change that must be agreed in the plan,
but it is not an unresolved electrical interpretation.

### I2C bus rates

The schematic uses 10 kOhm external pull-ups on both I2C buses. Legacy Media
firmware requests 1 MHz for the SSD1306 display bus, and the owner reports that
the same 10 kOhm board pull-ups worked at that rate on v1. The display module may
also contain onboard pull-ups; if those are 10 kOhm, the two sets produce about
5 kOhm effective resistance. The exact module resistor value is not recorded,
so this explanation is plausible but not yet verified.

The [SSD1306 timing table](https://www.sunrom.com/download/SSD1306.pdf)
specifies a minimum 2.5 microsecond I2C clock cycle, equivalent to 400 kHz, and
a maximum 300 ns rise time. Thus 1 MHz is a known-working board-specific
overclock, not a supported controller setting. The
[general I2C specification](https://www.nxp.com/docs/en/user-guide/UM10204.pdf)
sets a tighter 120 ns rise-time limit for Fast-mode Plus at 1 MHz.

With `tr = 0.8473 * R * C`, the approximate maximum bus capacitance at 120 ns
is only 14 pF with 10 kOhm, 28 pF with two parallel 10 kOhm pulls, or 44 pF if a
plausible module pull-up makes the effective resistance about 3.2 kOhm. At the
SSD1306's 300 ns limit, a single 10 kOhm pull-up permits about 35 pF. These
figures explain how a short v1 assembly could work at 1 MHz, but do not make the
rate portable across display modules or wiring.

The current driver sends a 7-byte address-window transaction and four
129-byte framebuffer pages for each 128x32 refresh. Including address and ACK
bits, ideal bus occupancy is approximately 4.8 ms at 1 MHz or 11.9 ms at
400 kHz, both comfortably within the 50 ms display interval. The recommended
supported 400 kHz would be the conservative default, but the owner selected the
measured v1 behavior: retain 1 MHz as an intentional overclock, use 400 kHz as
the bring-up fallback, and disable MCU internal pull-ups because the schematic
provides external pulls. Power remains at 100 kHz with external pull-ups.

### Media I2S module and channel strap

The firmware preset is specifically `SPH0645`, 32 kHz, 32-bit Philips I2S, and
the left channel. The schematic connector exposes 3.3 V, ground, BCLK, data, and
LRCK but does not expose the microphone module's left/right select input. The
migration should preserve the existing left-channel preset. If the migrated
Media node clocks correctly but receives silence, the breakout's local channel
strap and five-pin cable order are the first hardware checks; they need not
block the GPIO change.

## Resolved: Power node identity and former 8-bit routing limit

The code currently has no `Totem::Data::NodeName::Power` or corresponding
`NodeTraits`. `Totem::Data::PubSub::NodeId` and the PubSub backend's `NodeId`,
`PeerId`, and `PeerMask` are all 8-bit.

All eight bits are assigned:

| Bit | Current node |
| --: | ------------ |
|   0 | Master       |
|   1 | Media        |
|   2 | InputOutput  |
|   3 | GPU0         |
|   4 | GPU1         |
|   5 | GPU2         |
|   6 | GPU3         |
|   7 | Host         |

The original alternatives were:

1. Bring up only the physical SPI slave and clock sync for now, without making
    Power a routed PubSub node.
2. Retire or alias an existing routed role (not safe to assume; future GPU2/3
    remain documented targets).
3. Widen the PubSub node/peer mask and serialized source ID to 16 bits, which is
    a wire-format change affecting every active node and host tool.

The owner selected option 3 and then assigned the final bit layout: widen the
semantic and serialized PubSub node ID to 16 bits in one synchronized migration,
give Power Host's former bit, and move Host to the highest bit.

| Bit   | Final node / use |
| ----: | ---------------- |
|     0 | Master           |
|     1 | Media            |
|     2 | InputOutput      |
|     3 | GPU0             |
|     4 | GPU1             |
|     5 | GPU2             |
|     6 | GPU3             |
|     7 | Power (`0x0080`) |
|  8–14 | reserved         |
|    15 | Host (`0x8000`)  |

No existing device role is retired or aliased. This assignment does not change
the already accepted serialized or RAM overhead estimate for the 16-bit
migration.

## 16-bit node-ID feasibility and overhead

Status: feasible with a contained shared-protocol migration, but not backward
wire-compatible. The findings below are analysis only; no type or wire-format
change has been made.

### Serialized overhead

`PubSubBackend::Wire::Header` currently serializes these fields in order:

| Field                                | Current bytes | With 16-bit node ID |
| ------------------------------------ | ------------: | ------------------: |
| millisecond timestamp                |             4 |                   4 |
| microsecond timestamp                |             8 |                   8 |
| message ID                           |             4 |                   4 |
| topic ID                             |             4 |                   4 |
| source node ID                       |             1 |                   2 |
| traffic class                        |             1 |                   1 |
| payload size                         |             2 |                   2 |
| **Header total**                     |        **24** |              **25** |
| CRC                                  |             4 |                   4 |
| **Frame overhead excluding payload** |        **28** |              **29** |

Therefore widening the source ID costs exactly one serialized byte per PubSub
frame. Representative relative increases are:

| Payload                              | Current frame | 16-bit frame | Increase |
| ------------------------------------ | ------------: | -----------: | -------: |
| 0 bytes                              |            28 |           29 |    3.57% |
| PubSub control, 5 bytes              |            33 |           34 |    3.03% |
| Beat/Peak event, 8 bytes             |            36 |           37 |    2.78% |
| FFT frame, 16 bytes                  |            44 |           45 |    2.27% |
| Animation play, 39 bytes             |            67 |           68 |    1.49% |
| Maximum configured payload, 64 bytes |            92 |           93 |    1.09% |

For exact-length transports, this is one additional transmitted byte. At the
current 921600-baud RS485 setting it is approximately 10.85 microseconds per
frame under an 8-N-1 assumption. UDP cost is negligible relative to packet
overhead.

SPI uses fixed transaction buckets, so the usual cost is zero physical bus bytes
rather than one. A single inner PubSub frame requires:

`19-byte SlotHeader + 10-byte FrameHeader + 28-byte PubSub overhead + payload`

That is currently `57 + payload` bytes and becomes `58 + payload`. The 64-byte
bucket consequently changes from accepting payloads up to 7 bytes to accepting
payloads up to 6 bytes. A payload of exactly 7 bytes would jump from a 64-byte
to a 256-byte SPI transaction, adding 192 clocked bytes (153.6 microseconds at
10 MHz).

The generated schema was checked systematically. `SpokeSweepConfig` is exactly 7
bytes, but it is not published as a PubSub payload; it is encoded into the fixed
39-byte `AnimationPlayCommand.payload` field. Production top-level payloads are
1, 2, 4, 5, 8, 16, 32, 36, or 39 bytes (Wheel is the known 4-byte generated-code
exception). No production top-level 7-byte payload is present, so no current
single-frame transaction is expected to make the 64-to-256-byte jump. Future
top-level payload additions must retain a boundary test. Larger buckets can also
cross a boundary when several frames are coalesced, although this is
workload-dependent rather than an inherent per-frame jump.

### RAM and code-structure effects

Target-toolchain layout probes were compiled for both the ESP32-S3 Xtensa ABI
and ESP32-C3 RISC-V ABI. Both produced the same results:

| In-memory type                     | 8-bit/current | 16-bit/straightforward | Delta |
| ---------------------------------- | ------------: | ---------------------: | ----: |
| `Header`                           |            32 |                     32 |     0 |
| `Envelope`                         |            56 |                     56 |     0 |
| `IngressContext`                   |             2 |                      4 |    +2 |
| `TransportDispatch`                |             4 |                      8 |    +4 |
| queued RX frame                    |            96 |                    100 |    +4 |
| queued raw frame                   |           128 |                    136 |    +8 |
| point-to-point in-flight frame     |           208 |                    216 |    +8 |
| router in-flight frame             |           160 |                    168 |    +8 |
| `TransporterEntry`, 8 slots        |            56 |                    n/a |   n/a |
| `TransporterEntry`, 9 slots        |           n/a |                     60 |    +4 |
| `TransporterEntry`, naive 16 slots |           n/a |                     88 |   +32 |

The in-memory `Header` and `Envelope` do remain unchanged because existing
alignment padding absorbs the source byte. Arena storage is fixed-capacity and
does not grow. Transient ingress/dispatch values grow slightly.

Fixed transport frame buffers grow from 92 to 93 logical bytes. In the current
field layout that one byte crosses several structure-alignment boundaries. At
the configured queue depth of eight, a normal SPI or RS485 transport therefore
adds 32 bytes of RX queue storage, 64 bytes of deferred-raw queue storage, and
64 bytes of in-flight storage: 160 persistent bytes per transport. An SPI router
has that cost per configured physical peer. UDP has two eight-deep RX
frame-shaped queues and therefore adds 64 bytes per UDP transport.

This alignment amplification is optional rather than inherent. A second ABI
probe changed only the queued record `size` field from 32-bit `size_t` to a
bounded 16-bit value. The 93-byte RX/raw/in-flight records then fit in exactly
the same 96/128/208 bytes as the current 92-byte records on both targets. That
would need a compile-time assertion that the fixed frame capacity fits in 16
bits and explicit checked conversions. It is a reasonable direct optimization if
the owner wants the minimum RAM cost, but it expands the shared change beyond
merely widening node IDs.

The larger avoidable RAM cost is the routing table. `detail::maxPeerCount` is
currently derived from the number of bits in the node-ID underlying type. A
naive type change therefore expands every per-transport peer-topic-mask array
from 8 to 16 entries:

- 32 additional bytes per `TransporterEntry`
- approximately 256 persistent bytes per PubSub instance at the configured
    maximum of eight transports
- approximately another 256 bytes in a temporary transport snapshot

Only nine peer slots are currently assigned, so it would be possible to decouple
allocated slots from integer width. The owner has explicitly rejected that
micro-optimization for this migration. The straightforward implementation will
let the existing width-derived table expand from 8 to 16 entries and will verify
the resulting firmware maps; no queue packing or nine-slot semantic limit should
be added speculatively.

The detailed nine-slot estimates are retained only as bounds on what could be
optimized later. With the accepted direct 16-slot expansion, a simple node is
still expected to grow by only a few hundred bytes and Master by roughly 1 KiB
after its second low-speed peer. Small containing-object padding can shift the
whole-object totals, so the final firmware map deltas will be recorded after an
approved implementation rather than optimized in advance.

The bounded queue-length and nine-slot variants above are reference-only
optimization bounds. The approved straightforward conversion keeps the existing
queue-length representation and direct width-derived 16-slot table; the
resulting small RAM increase will be accepted and measured.

CPU overhead should be very small on both 32-bit MCUs: 16-bit mask operations
remain native-width integer work, serialization adds one byte store/load, and
the CRC processes one extra byte per frame. Flash change cannot be measured
honestly until the synchronized implementation exists, but no new algorithm,
allocation, or per-message search is required; only a small code-size delta is
expected. Final map and timing deltas should be measured rather than inferred.

### Required shared changes

Most code uses aliases and would adapt mechanically, but a clean conversion must
cover all of these shared surfaces together:

- `Data::PubSub::NodeId` and the backend `NodeId`, `PeerId`, and `PeerMask`
- the `Data::NodeName`/`NodeTraits` entry for Power
- serialized PubSub header encode/decode and header-size calculations
- the existing width-derived routing arrays, which may expand directly to 16
    entries as approved
- the generated C++/Python schema bindings, where Python currently records
    `NodeId` and `Header.source` as one byte
- the native `tools/pubsub-udp-peer/pubsub_wire.hpp` host implementation, which
    independently hard-codes a one-byte source ID
- affected protocol/layout tests and protocol documentation

The SPI `SlotHeader.peerId` is a separate link-local 8-bit field and does not
need to widen with the routed PubSub node ID.

`PubSubBackend/detail/Concepts.hpp` also contains a stale `kHeaderWireSize`
capacity calculation: it currently omits the 8-byte microsecond timestamp and
the 1-byte traffic class, understating the actual serialized header by nine
bytes. This is pre-existing. The migration should replace that duplicated
formula with the canonical serializer size (or make the formula complete),
rather than merely changing its node-ID term.

`magic_enum` needs an explicit flags specialization for `NodeId`. The current
unspecialized common-enum scan already omits `Host = 1 << 7` at its default
upper boundary and would not represent the final `Host = 1 << 15`. Setting
`enum_range<NodeId>::is_flags = true` makes magic-enum scan the one-hot bit
positions of the 16-bit underlying type, as the project already does for
`Topic`; a large numeric min/max scan is neither needed nor desirable. Targeted
tests must assert that both `Power = 0x0080` and `Host = 0x8000` are reflected.

### Compatibility decision

Changing the serialized field from one to two bytes shifts every following
header field. There is no version or magic field in the inner PubSub header, so
old and new firmware will not interoperate; they will misparse the remaining
header and ordinarily reject the frame through length/CRC checks. All firmware
nodes and native host peers therefore require a synchronized deployment.

The outer SPI slot has its own version, but that does not version PubSub on
RS485 or UDP and should not be treated as an inner-protocol compatibility
mechanism. Adding an explicit PubSub wire-version field now would improve future
diagnostics/migrations but would add at least one more byte and expand the scope
beyond the approved minimum conversion. It is not proposed for this migration.

No clean one-byte encoding remains while `NodeId` is also the one-hot routing
mask and all eight current roles remain allocated. Aliasing or retiring a role
would be electrically unrelated technical debt; a variable extension for only
Power would make every decoder more complex. Subject to the measured ABI result,
a direct 16-bit atomic protocol migration is the cleanest long-term option and
has modest overhead.

## Owner inputs resolved; one pre-plan repository question remains

The owner has resolved every electrical value/topology input which firmware
could not infer safely:

1. U8 and U9 both use fitted 2 mOhm shunts.
2. INA logic power, I2C pull-ups, and the controller are all on `POWER_3V3` so
    the Power node can bring up both monitors without the Master populated;
    the RS485 HAT remains on `MASTER_3V3`.
3. U9 was corrected to the common output side of SW1 and now measures either
    selected source's total load through C+/C-.
4. Installed S3 USB links are data-only, the C3 has no in-situ USB connection,
    and USB power is not selected while external power is connected.
5. C+/C- are the high-current shunt terminals; IN+/IN- are post-4.7 Ohm internal
    sense nodes and should be left externally unconnected.

One worktree change is strange enough to confirm before planning: the current
diff deletes `schematic/lib/connectivity_contract.py` and removes its invocation
from the generator. The old contract cannot remain unchanged because the
intentional U8/U9 pin groups changed, but updating its baseline would preserve
the exact-pin regression guard. The owner should confirm whether complete
removal was intentional or whether the migration should restore and update it.

The stated battery thresholds map cleanly onto the existing two-level model:
retain the normal 7S practical window of 21.0--29.4 V and move the absolute
error window to approximately 20.0--30.0 V. The shared classifier uses strict
comparisons (`value < absoluteMin` and `value > absoluteMax`), while
`BatteryConfig` stores per-cell integer millivolts. To make a measured 20.000 V
low-error and 30.000 V high-error without changing shared semantics, use 20.006
V (`2858 mV * 7`) as the stored lower boundary and 29.995 V (`4285 mV * 7`) as
the stored upper boundary. Nominal capacity can start at the owner's provisional
6,000 mAh and later be replaced by calibration. No “all LEDs red” response
exists in the current requested data path; that future system response should be
designed separately once the desired fault consumer and latching/reset behavior
are defined.

The proposed implementation defaults which do not block planning are:

- fail Power startup if either required INA226 cannot be identified/configured;
- calibrate U8 for 3 A, with 2 A practical-high and 3 A absolute-high current
    bounds; calibrate U9 for 1 A, with 500 mA practical-high and 1 A
    absolute-high bounds;
- use the existing 5 mA reverse-current deadband as each practical-low default
    and a symmetric negative absolute range unless bring-up shows that expected
    reverse current needs different treatment;
- use Power I2C at 100 kHz and Media I2C at the selected intentional 1 MHz
    overclock, both with MCU internal pull-ups off;
- use audio-tools v1.2.5 for Media;
- enable PSRAM on Master and preserve/enable it on Media, GPU0, and GPU1; C3
    Power and IO have no PSRAM; and
- do not add a Power telemetry payload beyond local INA/BatteryMonitor metrics.

## Concrete change-surface inventory

This is an unordered completeness inventory rather than an execution plan.

### Schematic hardening

- add four 10 kOhm CS pull-ups, likely `R23`–`R26`, on the slave side of the
    existing series resistors;
- connect all four pull-ups to `MASTER_3V3` as specified by the owner; retain
    U3/GPU0's 3.3 V pin as no-connect;
- add a 100 nF ceramic decoupler directly between U6 pin 14 (`VCC_5V`) and pin
    7 (`GND`), likely as `C1` because the generated schematic currently has no
    capacitors;
- regenerate the schematic and both netlists, then run ERC and verify the four
    exact CS-to-`MASTER_3V3` paths; and
- do not restore the deleted agent-only connectivity contract.

### Shared identity and wire consumers

- `include/Data/Nodes.hpp`: add semantic `Power`.
- `include/Data/PubSub.hpp`: widen `NodeId`, replace Host's old bit with
    `Power = 0x0080`, move Host to `0x8000`, add Power's `NodeTraits`, and mark
    the enum as magic-enum flags so all 16 one-hot positions are reflected.
- `include/PubSubBackend/Interfaces/Types.hpp`: widen backend `NodeId` and
    `PeerId` together.
- `include/PubSubBackend/detail/Concepts.hpp`: accept the 16-bit semantic ID and
    replace the stale duplicated minimum-header formula with the canonical
    serialized header size.
- generated C++ and Python wire metadata: regenerate after the semantic type
    change; the generator already derives enum and field widths from C++.
- `tools/pubsub-udp-peer/pubsub_wire.hpp`: widen its independent host
    `NodeId`/header source codec and size calculation. The Python bridge already
    represents source IDs as unrestricted integers and needs no binary-codec
    change.
- `include/Setups/PubSubStarTest.hpp`: remove its remaining eight-bit source
    cast so the regression harness can compare Power without truncation. The
    broader local harness already uses a 32-bit test node mask.
- update peer-mask diagnostic formatting in the touched 16-bit paths so logs do
    not misleadingly show only two hexadecimal digits; avoid unrelated logging
    cleanup.

The outer SPI `SlotHeader.peerId` remains an unrelated eight-bit link-local
field.

### Shared link lifecycle

- add an atomic Master attention-armed state which is clear on begin, end, and
    every link reset, is set only after a successful handshake, and gates both
    ISR wakeup/recording and held-low polling;
- keep periodic hello retries independent of attention so gating cannot prevent
    SPI link establishment;
- gate RS485 Master ISR/notify recording and held-low sampling on its existing
    atomic synced state; its periodic hello/UART path remains independent; and
- enable the weak internal pull-up on each slave CS input during SPI slave
    initialization as defense in depth behind the recommended external 10 kOhm
    pull-ups.

### Master

- drive all four active-low CS GPIOs high at the start of `setup()`, before the
    three-second startup delay and before any per-device SPI task can run;
- derive a Power low-speed link config from the Media Bus3 config, changing only
    CS to GPIO6, attention to GPIO9, and the task name;
- instantiate/begin/register the additional physical SPI Master link;
- make the low-speed PubSub transport a two-peer router for Media and Power;
- arm SPI attention handling only after each link's handshake is ready; and
- use USB Serial/JTAG as the primary console so GPIO43/GPIO44 belong solely to
    GPU1 attention/CS.

### Power

- enable only SPI and I2C components in `env:power`;
- use SPI2 slave pins CS0/MOSI1/SCLK3/MISO4/ATTN10;
- use I2C0 SDA7/SCL `StrappingGPIO8`, 100 kHz, external pull-ups;
- instantiate the SPI clock/PubSub edge exactly as on Media/GPU nodes;
- instantiate U8 at `0x40` and U9 at `0x41` with unique metrics group names;
- configure both for 2,000 uOhm shunts; use a 3,000,000 uA expected/absolute
    maximum and 2,000,000 uA practical maximum for U8, and a 1,000,000 uA
    expected/absolute maximum and 500,000 uA practical maximum for U9;
- feed only U8 samples into BatteryMonitor and run U9 only for ordinary
    sampling/metrics;
- register the existing BatteryMonitor command adapter for local status and
    later calibration control;
- add the normal tracked `data/power/littlefs` validation content so Power's
    BatteryMonitor filesystem can be built/uploaded and checked like other
    production nodes;
- retain the disabled status LED because its C3 board alias is the live SCL pin;
    and
- service clock sync, both INA instances, BatteryMonitor, and Core in the main
    loop. No separate I2C-master work loop is required.

The schematic polarity is correct for the module and driver's convention: both
monitors place C+ upstream and C- downstream, so normal load current is
positive. U8 includes both the buck input and J6 load. U9 is after SW1 and
therefore includes total common-rail load from either selected 5 V source.

### Media

- replace every legacy pin with the v2 S3-Zero mapping already tabulated;
- use `Pin::StatusLed` for GPIO21 and `Pin::RX` for the GPIO44 beat indicator;
- set the LedPwm platform output inversion for the active-low beat LED;
- disable MCU internal display-bus pull-ups and retain the owner-selected 1 MHz
    intentional overclock, with 400 kHz documented as a bring-up fallback;
- remove the forced original-ESP32 target macro and inherit S3-Zero CMake
    features, including PSRAM;
- use audio-tools v1.2.5; and
- retire the commented original-ESP32 `media-btstack` environment and its
    Classic-Bluetooth sdkconfig overlay; retain shared source implementations;
- use USB Serial/JTAG as primary console. The existing I2S source format and
    left-channel selection remain unchanged.

### Build and memory configuration

- make Media inherit `${esp32s3zero.board_build.cmake_extra_args}` instead of
    the original-ESP32 arguments, and remove its forced original-ESP32 target
    macro;
- remove Master's obsolete v1 PSRAM disable/overlay so its S3-Zero uses PSRAM;
- preserve the already enabled S3 PSRAM configuration for GPU0/GPU1 and the
    corrected Media configuration; and
- retain `CONFIG_SPIRAM=n` for C3 Power and IO because those MCUs have no PSRAM.

### GPU and other active nodes

GPU0 needs no firmware pin edit. GPU1's corrected LED1 path assigns clock to
GPIO10 and data to GPIO13, so its SK9822 output configuration must use that
ordering while preserving the existing output-gate work. Rebuild both GPU
targets because they share the same source configuration. The four
`MASTER_3V3` CS pull-ups do not alter a GPIO assignment. IO has no schematic
GPIO changes, but it must rebuild because it participates in the atomic PubSub
wire migration. AI, scratch, and wheel are outside this board and do not need
GPIO edits.

## Verification and documentation audit

The synchronized software verification surface is:

- regenerate wire metadata with `make wire PIO_ENV=master`;
- build the native UDP peer so its independent 16-bit codec cannot drift;
- add round-trip assertions that `NodeId::Power == 0x0080` and
    `NodeId::Host == 0x8000` survive the PubSub serializer and native host codec,
    that both magic-enum names are visible, and that the outer SPI slot peer byte
    remains link-local;
- exercise the two-peer low-speed router with distinct Media/Power masks so the
    widened bit cannot be truncated or sent to the wrong CS;
- build `master`, `media`, `power`, `gpu0`, `gpu1`, and `io`;
- run the existing BatteryMonitor host tests and inspect all SARIF output;
- record final Master/Power RAM, flash, and configured task-stack reports; and
- verify default non-inverted LedPwm compilation through IO as well as the
    inverted Media configuration.

Hardware bring-up must remain staged because a successful build cannot validate
electrical routing:

- confirm U8/U9 identity logs at `0x40`/`0x41` and compare both rails against a
    meter before trusting current/power;
- verify positive current under a known load and the fitted shunt scaling;
- establish Media and Power links independently before running both on the
    shared low-speed bus;
- hold/assert each Master attention input before handshake and verify that it
    causes no attention-driven transaction until that individual link is ready;
- confirm each unselected low-speed slave releases MISO before sustained
    dual-peer traffic;
- exercise Media status LED, active-low beat LED, display, and I2S input; and
- regression-check both GPU links, output gate, strobe, and SK9822 output.

Documentation/tooling requiring updates after approval is now bounded:

- `docs/overview.md`, `docs/pubsub.md`, and `docs/commands.md` for the six-node
    active graph and Power build/monitor surface;
- `docs/audio.md` for S3 pins and the active-low GPIO44 indicator;
- `platformio.ini`/`sdkconfig.stack.media-btstack` to retire the dead
    original-ESP32 Classic-Bluetooth Media environment;
- `docs/battery-monitor.md` and the maintained BatteryMonitor implementation
    record for the production U8/U9 mapping, 6 Ah provisional capacity, and
    20/30 V absolute bounds;
- `sdkconfig.stack.master`'s obsolete GPIO36/GPIO37 PSRAM explanation; and
- `bin/task-stacks` for Master's Power SPI task and Power's SPI/PubSub tasks.

The future SD-card/controller conflict is settled for this prototype by the
owner's bit-banged-SD direction. Its implementation remains outside the present
GPIO/INA migration. The agent-only connectivity contract was intentionally
deleted and must not be restored. Media's known-working 1 MHz display overclock
is retained. All electrical observations above are now either settled migration
inputs or bring-up notes under the owner's pragmatic threshold; no discussion
gate remains.

## Known documentation drift

- `docs/overview.md` still says the Media peak LED is active-high GPIO26.
- `docs/overview.md` and `sdkconfig.stack.master` still describe Master
    low-speed SPI on GPIO36/GPIO37 and the resulting PSRAM conflict.
- `docs/audio.md` contains legacy Media I2S and peak-indicator GPIOs.
- `docs/audio.md` and the commented `media-btstack` configuration still present
    Classic A2DP as an original-ESP32 Media option which the S3 cannot provide.
- `docs/battery-monitor.md` still describes Power integration, U8/U9 addresses,
    10 Ah capacity, and USB calibration procedure as pending/provisional.
- Current-scope docs omit Power from the active logic-board node list.

Documentation changes are authorized as part of implementation.

## Worktree safety

The repository already has extensive modified and untracked work, including
Master/GPU GPIO and SK9822 changes made before this investigation. Any later
implementation must preserve those changes and make scoped, reviewable edits.

## Investigation log

### 2026-08-26

- Read the repository overview and command documentation.
- Read the KiCad analysis skill and followed its raw-file cross-check guidance.
- Located and compared the schematic source, standard netlist, configuration
    netlist, generated schematic, ERC report, and pin connectivity contract.
- Regenerated the schematic in temporary storage; pin contract passed and no
    electrical ERC errors were reported.
- Ran the schematic analyzer and triaged its high-severity output. Retained the
    INA226 address conflict as actionable; classified its missing-pull-up and
    voltage-domain-level results as custom-symbol/rail-inference limitations.
- Audited current Master, Media, GPU, and Power configuration files and the
    relevant PlatformIO environment inheritance.
- Identified the full 8-bit PubSub node-ID allocation as a blocker for a routed
    Power node.
- Traced the node-ID type through the PubSub wire header, routing masks,
    generated bindings, and native UDP host peer. A 16-bit ID adds exactly one
    serialized byte per frame but is an atomic, non-backward-compatible protocol
    change.
- Identified the avoidable 8-to-16 routing-array expansion and the bounded
    8-to-9-slot alternative for adding Power at bit 8.
- Identified the SPI 64-byte bucket edge case for exactly 7-byte application
    payloads and the stale shared header-capacity formula.
- Checked every generated message size: the sole 7-byte type is a nested
    animation config, not a top-level publication, so current traffic does not
    hit that SPI bucket edge.
- Measured S3 and C3 ABI layouts with their target compilers. The
    straightforward queue layout adds 160 bytes per SPI/RS485 transport (or
    router peer), while a bounded 16-bit queued-length field absorbs the
    frame-byte increase without growing those records.
- Rechecked `media_io()` and removed an erroneous duplicate-J7 finding: the
    current source and generated netlist each contain exactly one display
    connector.
- Identified the S3 primary UART0 console overlap with Master GPU1 attention/CS
    and Media's beat-LED pin.
- Identified the INA226 custom-module terminal topology, microphone channel
    strap, and GPU1 level-shifter power-up enable as hardware details that
    cannot safely be inferred from generic IC behavior.
- Ran a fresh Media diagnostic build. It resolves to the ESP32-S3 toolchain but
    fails from the simultaneous forced original-ESP32 target macro, legacy
    GPIOs, and target-confused audio-tools code; inherited CMake arguments also
    disable the S3 PSRAM component.
- Rebuilt Media in an isolated temporary configuration. Correct S3 flags remove
    the ESP-IDF target conflicts, but pinned audio-tools v1.2.2 still has an
    ESP-IDF timer-type failure. The exact upstream v1.2.5 tag removes that
    failure; only the already-known legacy GPIO constants then remain as errors.
- Built the current no-peripheral Power baseline successfully and traced the
    existing SPI slave, low-speed Master bus sharing, clock sync, PubSub edge,
    I2C master, dual INA226, metrics, and BatteryMonitor composition points.
- Built the current Master, GPU0, and GPU1 baselines successfully. This confirms
    the pre-existing v2/SK9822 mappings compile before the remaining migration.
- Re-traced the outer SPI slot peer byte and confirmed it is not populated from
    the routed one-hot PubSub peer mask, so it need not widen for Power; a
    regression test is still required to preserve that separation.
- Identified un-biased active-low SPI chip selects as a reset/staggered-power
    MISO-contention risk that firmware alone cannot eliminate.
- Identified missing schematic decoupling for the bare 74AHCT125 and missing
    identity/electrical evidence for the RS485 HAT and its off-board
    single-ended attention line.
- Confirmed that the C3's sole general-purpose SPI2 controller is consumed by
    the Power slave link and that the C3 has no SDMMC host, blocking the stated
    future hardware-SD role without a software-SPI, topology, or MCU decision.

### 2026-08-27

- Completed a repository-wide GPIO-use search. No hidden in-scope production
    mappings exist outside the tabulated Master/Media/GPU configs, Master
    strobe, and the new Power configuration surface.
- Completed the generated/native-host consumer audit for the 16-bit ID. The
    generator derives enum field width, while the standalone C++ UDP peer and
    one star-test cast require explicit changes.
- Confirmed the direct 16-slot routing-table expansion requested by the owner
    remains small; removed queue-packing/nine-slot ideas from the proposed
    scope.
- Measured complete SPI link/PubSub-edge object sizes and checked Master/Power
    task-registry capacity. The second low-speed link fits without architecture
    changes and leaves Master one task-registry slot.
- Extended the pre-establishment attention finding to RS485: its Master path can
    also retain a pre-sync request, but its existing atomic sync state and
    independent hello/UART path provide a contained gate.
- Confirmed the installed SPI slave driver does not enable CS pull-ups. The
    migration surface initially included early Master CS-high drive and slave
    internal pull-ups; the later owner-requested hardware review promoted four
    external 10 kOhm CS pull-ups to the recommended schematic change.
- Corrected the battery threshold mapping for strict comparator semantics:
    stored 20.006/29.995 V boundaries make measured 20.000/30.000 V error
    states.
- Initially traced U9 as measuring only the buck output branch; the owner then
    corrected the schematic, and the final regenerated topology is recorded
    below.
- Audited Media's remaining original-ESP32 artifacts. The active build needs S3
    flags/PSRAM and audio-tools v1.2.5; the commented Classic-Bluetooth BTstack
    environment is not S3-capable and conflicts with the sidecar direction.
- Completed the build/test/tool/document coverage inventory, including Power
    LittleFS content, BatteryMonitor commands, task-stack reporting, staged bus
    validation, and synchronized six-node wire deployment.
- Rechecked the owner-corrected Power source path. It now runs each selected SW1
    source through U9 C+/C- before common `VCC_5V`, so U9 measures total
    common-rail load. U8 likewise uses C+/C- in the 24 V series path; all INA
    IN+/IN- module pins are intentionally external NCs because the module routes
    the shunt to those internal sense nodes through 4.7 Ohm resistors.
- Regenerated both netlists into temporary storage. The configuration netlist is
    byte-identical to `schematic/build`; the KiCad netlist differs only in
    timestamp/source metadata. SKiDL reported zero ERC errors and KiCad reported
    zero errors (91 known library-context warnings).
- Validated the 2 mOhm current ranges against the TI INA226 limits and the
    repository calibration implementation. U8's 3 A range and U9's 1 A range
    both configure cleanly, with respective positive full scales of about 3.015
    A and 2.589 A.
- Recorded the in-situ data-only USB arrangement and mutually exclusive
    external/USB power rule, resolving the earlier VBUS-paralleling concern as
    an operational constraint.
- Found that the current worktree deletes the exact pin-connectivity contract
    instead of updating it for the new U8/U9 topology; the owner confirmed this
    was an intentional deletion of an agent-only artifact, so it will not be
    restored.
- Recorded the final 16-bit identity allocation: Power takes bit 7 (`0x0080`),
    Host moves to bit 15 (`0x8000`), and bits 8–14 remain reserved. This has no
    overhead change relative to the already approved 16-bit estimate.
- Audited PSRAM configuration after the owner lifted the v1 restriction. Master
    must remove its obsolete GPIO36/GPIO37 disable, Media must inherit S3-Zero
    component arguments, GPU0/GPU1 already request PSRAM, and C3 Power/IO cannot
    provide it.
- Checked all four CS endpoints against the MCU strap pins. Four 10 kOhm
    pull-ups are safe, add only 0.33 mA while selected, and close the reset-time
    shared-MISO risk more effectively than firmware alone. The initial
    implementation incorrectly inferred slave-local rail ownership; the owner
    corrected this before firmware work, so all four pull-ups use
    `MASTER_3V3` and GPU0 3.3 V remains NC.
- Verified the SSD1306 2.5 microsecond minimum clock cycle and quantified the
    current full-frame transfer: about 4.8 ms at 1 MHz versus 11.9 ms at
    supported 400 kHz inside a 50 ms refresh interval. The owner selected the
    proven 1 MHz overclock; MCU internal pull-ups will be disabled.
- Received implementation authorization for all analyzed migration surfaces,
    including four 10 kOhm CS pull-ups and U6's 100 nF decoupler.
- Recorded permission for tightly scoped quality improvements in migration-
    touched code, including correct 16-bit peer-mask diagnostics and explicit
    protocol assertions, without unrelated refactoring.

## Implementation results

Implementation completed on 2026-08-27 against the regenerated v2
configuration netlist.

- Widened `NodeId` and PubSub peer identities to 16 bits end to end. `Power`
  now owns `0x0080`, `Host` owns `0x8000`, generated Python bindings use the
  same values, and the standalone UDP peer uses the new 25-byte outer header.
  A host regression test round-trips the high-bit Host identity.
- Converted the Master's low-speed transport from a point-to-point Media link
  to a two-peer Media/Power router. Master uses GPIO6 for Power CS and GPIO9
  for Power attention while retaining the existing v2 bus pins. All four CS
  outputs are driven high during early startup.
- Populated the Power C3 node with Core, clock synchronization, a PubSub SPI
  slave, one I2C master, both INA226 devices, and BatteryMonitor commands. U8
  is address `0x40`, uses a 2 milliohm shunt, and is the sole BatteryMonitor
  source; U9 is address `0x41`, uses a 2 milliohm shunt, and publishes ordinary
  INA metrics/limits. The provisional pack capacity is 6 Ah.
- Migrated Media to the ESP32-S3 Zero pin map, native USB console, S3 PSRAM,
  audio-tools v1.2.5, active-low GPIO44 peak LED, and GPIO21 status LED. Its
  externally pulled display bus retains the owner-selected 1 MHz clock with
  MCU internal pull-ups disabled.
- Enabled PSRAM for the S3 Master and Media builds. GPIO43/GPIO44 are no longer
  reserved for a hardware-UART console on those nodes.
- Added pre-handshake attention gating to SPI Master and RS485 Master paths so
  reset-time or spurious attention cannot request normal traffic before the
  link is ready.
- Added 10 kOhm pull-ups to all four slave-side CS nets and a 100 nF U6 bypass
  capacitor in the schematic generator, then regenerated the schematic, PDF,
  KiCad netlist, and configuration netlist. All CS pull-ups are sourced from
  `MASTER_3V3`; GPU0's 3.3 V pin remains explicitly NC. C1 is connected between
  `VCC_5V` and GND at U6.
- Moved both INA226 module `VCC` pins from `MASTER_3V3` to the Power-owned
  `POWER_3V3` rail alongside the I2C pull-ups. This makes Power-only USB
  bring-up self-contained and removes the INA partial-power condition when the
  Master module is absent. The RS485 HAT remains on `MASTER_3V3`.
- Added green power indicator LED2 from U9 C- / `VCC_5V` through R27 (4.7 kOhm)
  to ground, so it indicates either selected source on the common rail.
- Re-audited every populated logic-board GPIO against the exported netlist.
  Master, Media, Power, GPU0, and GPU1 firmware mappings match. The IO node is
  off-board behind J2 and therefore has no MCU-pin mapping in this schematic;
  its Master-side RS485 GPIO14/GPIO15/GPIO16 mapping matches J2/U10.
- Generated bindings and all six active hardware images build successfully:
  `master`, `media`, `power`, `gpu0`, `gpu1`, and `io`. Final image usage was
  Master 217,388 B RAM / 1,087,623 B flash, Media 156,520 / 1,205,295, Power
  136,148 / 1,086,394, GPU0 204,852 / 1,131,807, GPU1 205,172 / 1,136,691,
  and IO 150,588 / 1,465,488.
- Host PubSub wire tests, the standalone UDP-peer compile test, and the complete
  BatteryMonitor C++/Python suite pass. The BatteryMonitor suite exposed and
  corrected a test boundary: the configured 28.000 V full-qualification value
  is accepted, so the below-full rejection sample is now 27.999 V.
- Final schematic regeneration reports zero SKiDL ERC errors and zero KiCad ERC
  errors. KiCad retains 101 known warnings caused by unavailable symbol-library
  and footprint context. Targeted Python formatting checks and `git diff
  --check` pass.
- Added Power's tracked `data/power/littlefs/littlefs.txt` validation fixture,
  matching the other production nodes. No firmware or filesystem image was
  flashed to Power or to any other hardware during this implementation.
