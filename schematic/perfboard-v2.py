#!/usr/bin/env python3

# ruff: noqa: PLR0913

"""Generate the perfboard-v2 logic-board reference schematic and netlists.

The KiCad schematic is split into small functional sheets so it remains useful
as a wiring reference. Two netlists are emitted from the same SKiDL circuit:

* ``perfboard-v2.net`` is the standard KiCad netlist.
* ``perfboard-v2.config-netlist.json`` maps logical signals to named module
  pins and is intended for deriving node firmware configuration.

The code below is intentionally limited to the electrical description. Library
setup, output normalization, and the pin-for-pin regression contract live in
``schematic/lib``.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from lib.kicad_output import generate_outputs
from lib.skidl_helpers import (
    SignalCatalog,
    configure_skidl,
    custom_part,
    make_net,
    mark_no_connect,
    resistor,
)
from skidl import POWER, Circuit, Net, Part, subcircuit

OUTPUT_BASENAME = 'perfboard-v2'
DEFAULT_OUTPUT_DIR = Path(__file__).resolve().parent / 'build'


@subcircuit
def master_controller(vcc: Net, gnd: Net, master_3v3: Net) -> Part:
    """Master ESP32-S3 module and its local power domain."""

    master = custom_part('Module_ESP32S3_Zero', 'U1')
    master.configuration_node = 'master'
    vcc.connect(master['5V'])
    gnd.connect(master['GND'])
    master_3v3.connect(master['3V3'])
    mark_no_connect(
        master,
        'GPIO17',
        'GPIO18',
        'GPIO38',
        'GPIO39',
        'GPIO40',
        'GPIO41',
        'GPIO42',
        'GPIO45',
    )
    return master


@subcircuit
def media_controller(vcc: Net, gnd: Net, media_3v3: Net) -> Part:
    """Media ESP32-S3 module and its local power domain."""

    media = custom_part('Module_ESP32S3_Zero', 'U2')
    media.configuration_node = 'media'
    vcc.connect(media['5V'])
    gnd.connect(media['GND'])
    media_3v3.connect(media['3V3'])
    mark_no_connect(
        media,
        'TX',
        'GPIO14',
        'GPIO15',
        'GPIO16',
        'GPIO17',
        'GPIO18',
        'GPIO38',
        'GPIO39',
        'GPIO40',
        'GPIO41',
        'GPIO42',
        'GPIO45',
    )
    return media


@subcircuit
def gpu_controllers(
    vcc: Net,
    gnd: Net,
    gpu1_3v3: Net,
) -> tuple[Part, Part]:
    """The two ESP32-S3 LED-rendering modules."""

    gpu0 = custom_part('Module_ESP32S3_Zero', 'U3')
    gpu0.configuration_node = 'gpu0'
    vcc.connect(gpu0['5V'])
    gnd.connect(gpu0['GND'])
    mark_no_connect(
        gpu0,
        '3V3',
        'GPIO3',
        'GPIO4',
        'GPIO5',
        'GPIO6',
        'GPIO9',
        'RX',
        'TX',
        'GPIO14',
        'GPIO15',
        'GPIO16',
        'GPIO17',
        'GPIO18',
        'GPIO38',
        'GPIO39',
        'GPIO40',
        'GPIO41',
        'GPIO42',
        'GPIO45',
    )

    gpu1 = custom_part('Module_ESP32S3_Zero', 'U4')
    gpu1.configuration_node = 'gpu1'
    vcc.connect(gpu1['5V'])
    gnd.connect(gpu1['GND'])
    gpu1_3v3.connect(gpu1['3V3'])
    mark_no_connect(
        gpu1,
        'GPIO7',
        'GPIO8',
        'GPIO11',
        'GPIO12',
        'RX',
        'TX',
        'GPIO14',
        'GPIO15',
        'GPIO16',
        'GPIO17',
        'GPIO18',
        'GPIO38',
        'GPIO39',
        'GPIO40',
        'GPIO41',
        'GPIO42',
        'GPIO45',
    )
    return gpu0, gpu1


@subcircuit
def power_controller(vcc: Net, gnd: Net, power_3v3: Net) -> Part:
    """ESP32-C3 power-monitoring module."""

    power = custom_part('Module_ESP32C3_Supermini', 'U5')
    power.configuration_node = 'power'
    vcc.connect(power['5V'])
    gnd.connect(power['GND'])
    power_3v3.connect(power['3V3'])
    mark_no_connect(power, 'GPIO2', 'GPIO5', 'GPIO6', 'GPIO9', 'RX', 'TX')
    return power


@subcircuit
def low_speed_spi(
    catalog: SignalCatalog,
    master: Part,
    media: Part,
    power: Part,
    master_3v3: Net,
) -> None:
    """Low-speed SPI bus from master to the media and power nodes."""

    catalog.series(
        'SPI0_MISO',
        'R1',
        '33',
        master['GPIO1'],
        media['GPIO11'],
        media['GPIO2'],
        power['GPIO4'],
    )
    catalog.series(
        'SPI0_CLK',
        'R2',
        '33',
        master['GPIO2'],
        media['GPIO10'],
        media['GPIO3'],
        power['GPIO3'],
    )
    catalog.series(
        'SPI0_MOSI',
        'R3',
        '33',
        master['GPIO3'],
        media['GPIO9'],
        media['GPIO4'],
        power['GPIO1'],
    )
    _, media_cs, _ = catalog.series(
        'SPI0_CS_MEDIA',
        'R4',
        '33',
        master['GPIO4'],
        media['GPIO8'],
    )
    catalog.series('SPI0_ATTN_MEDIA', 'R5', '1k', master['GPIO5'], media['GPIO7'])
    _, power_cs, _ = catalog.series(
        'SPI0_CS_POWER',
        'R6',
        '33',
        master['GPIO6'],
        power['GPIO0'],
    )
    catalog.series('SPI0_ATTN_POWER', 'R7', '1k', master['GPIO9'], power['GPIO10'])

    media_cs_pullup = resistor('R23', '10k')
    power_cs_pullup = resistor('R24', '10k')
    media_cs.connect(media_cs_pullup[1])
    power_cs.connect(power_cs_pullup[1])
    master_3v3.connect(media_cs_pullup[2], power_cs_pullup[2])


@subcircuit
def high_speed_spi(
    catalog: SignalCatalog,
    master: Part,
    gpu0: Part,
    gpu1: Part,
    master_3v3: Net,
) -> None:
    """High-speed SPI bus and frame-present strobe for the GPU nodes."""

    catalog.series('SPI1_ATTN_GPU0', 'R8', '1k', master['GPIO7'], gpu0['GPIO2'])
    _, gpu0_cs, _ = catalog.series(
        'SPI1_CS_GPU0',
        'R9',
        '33',
        master['GPIO8'],
        gpu0['GPIO1'],
    )
    catalog.series('STROBE', 'R10', '1k', master['GPIO10'], gpu0['GPIO10'], gpu1['GPIO4'])
    catalog.series('SPI1_MOSI', 'R11', '33', master['GPIO11'], gpu0['GPIO11'], gpu1['GPIO3'])
    catalog.series('SPI1_CLK', 'R12', '33', master['GPIO12'], gpu0['GPIO12'], gpu1['GPIO2'])
    catalog.series('SPI1_MISO', 'R13', '33', master['GPIO13'], gpu0['GPIO13'], gpu1['GPIO1'])
    _, gpu1_cs, _ = catalog.series(
        'SPI1_CS_GPU1',
        'R14',
        '33',
        master['RX'],
        gpu1['GPIO5'],
    )
    catalog.series('SPI1_ATTN_GPU1', 'R15', '1k', master['TX'], gpu1['GPIO6'])

    gpu0_cs_pullup = resistor('R25', '10k')
    gpu1_cs_pullup = resistor('R26', '10k')
    gpu0_cs.connect(gpu0_cs_pullup[1])
    gpu1_cs.connect(gpu1_cs_pullup[1])
    master_3v3.connect(gpu0_cs_pullup[2], gpu1_cs_pullup[2])


@subcircuit
def power_distribution(
    catalog: SignalCatalog,
    power: Part,
    vin: Net,
    vcc: Net,
    vusb: Net,
    gnd: Net,
    power_3v3: Net,
) -> None:
    """24 V input, 5 V conversion/selection, and rail monitoring."""

    buck = custom_part('Module_Buck_LM2596', 'U7')
    sense_24v = custom_part('Module_INA226', 'U8')
    sense_5v = custom_part('Module_INA226', 'U9')
    conn_power_in = Part('Connector', 'Conn_01x03_Socket', ref='J5', tag='J5')
    conn_power_out = Part('Connector', 'Conn_01x03_Socket', ref='J6', tag='J6')
    conn_usb = Part('Connector', 'Conn_01x04_Pin', ref='J8', tag='J8')
    switch_power = Part('Switch', 'SW_SPDT', ref='SW1', tag='SW1')
    power_led = Part('Device', 'LED', ref='LED2', tag='LED2', value='Green')
    power_led_resistor = resistor('R27', '4.7k')
    vin_power_flag = Part('power', 'PWR_FLAG', ref='#FLG01', tag='VIN_24V source')
    sense_power_flag = Part('power', 'PWR_FLAG', ref='#FLG02', tag='Sense source')

    vin.connect(conn_power_in[1], sense_24v['C+'], sense_24v['V+'], vin_power_flag[1])
    make_net('VIN_24V_SENSED', sense_24v['C-'], buck['IN+'], conn_power_out[1])
    make_net('VCC_5V_RAW', buck['OUT+'], switch_power[1])
    make_net('VCC_5V_SENSED', switch_power[2], sense_5v['C+'], sense_5v['V+'], sense_power_flag[1])

    gnd.connect(
        conn_power_in[3],
        conn_power_out[3],
        conn_usb[1],
        buck['IN-'],
        buck['OUT-'],
        sense_24v['V-'],
        sense_24v['GND'],
        sense_5v['V-'],
        sense_5v['GND'],
    )
    power_3v3.connect(sense_24v['VCC'], sense_5v['VCC'])
    vusb.connect(conn_usb[4], switch_power[3])
    vcc.connect(sense_5v['C-'], power_led[2])
    make_net('LED_POWER_K', power_led[1], power_led_resistor[1])
    gnd.connect(power_led_resistor[2])

    i2c_sda = catalog.direct('I2C_POWER_SDA', power['GPIO7'], sense_24v['SDA'], sense_5v['SDA'])
    i2c_scl = catalog.direct('I2C_POWER_SCL', power['GPIO8'], sense_24v['SCL'], sense_5v['SCL'])
    pullup_sda = resistor('R20', '10k')
    pullup_scl = resistor('R21', '10k')
    i2c_sda.connect(pullup_sda[1])
    i2c_scl.connect(pullup_scl[1])
    power_3v3.connect(pullup_sda[2], pullup_scl[2])

    power.circuit.NC.connect(
        conn_power_in[2],
        conn_power_out[2],
        conn_usb[2],
        conn_usb[3],
        sense_24v['IN-'],
        sense_24v['IN+'],
        sense_5v['IN-'],
        sense_5v['IN+'],
    )


@subcircuit
def rs485_interface(
    catalog: SignalCatalog,
    master: Part,
    master_3v3: Net,
    gnd: Net,
) -> None:
    """Master-side RS485 HAT and the off-board connector."""

    hat_rs485 = custom_part('Module_Hat_RS485', 'U10')
    conn_rs485 = Part('Connector', 'Conn_01x05_Socket', ref='J2', tag='J2')

    catalog.direct('RS485_RX', master['GPIO14'], hat_rs485['RX'])
    catalog.direct('RS485_TX', master['GPIO15'], hat_rs485['TX'])
    catalog.series('RS485_ATTN', 'R16', '1k', master['GPIO16'], conn_rs485[2])
    catalog.direct('RS485_A', hat_rs485['A+'], conn_rs485[3], stub=False)
    catalog.direct('RS485_B', hat_rs485['B-'], conn_rs485[4], stub=False)
    master_3v3.connect(hat_rs485['VCC'])
    gnd.connect(hat_rs485['GND'], conn_rs485[1])
    master.circuit.NC.connect(hat_rs485['S'], conn_rs485[5])


@subcircuit
def media_io(
    catalog: SignalCatalog,
    media: Part,
    media_3v3: Net,
    master_3v3: Net,
    gnd: Net,
) -> None:
    """Media-node display, I2S microphone, and beat indicator."""

    conn_i2s = Part('Connector', 'Conn_01x05_Socket', ref='J1', tag='J1')
    conn_display = Part('Connector', 'Conn_01x04_Pin', ref='J7', tag='J7')
    beat_led = Part('Device', 'LED', ref='LED1', tag='LED1', value='Blue')

    display_scl = catalog.direct('I2C_DISP_SCL', media['GPIO5'], conn_display[3])
    display_sda = catalog.direct('I2C_DISP_SDA', media['GPIO6'], conn_display[4])
    pullup_scl = resistor('R17', '10k')
    pullup_sda = resistor('R18', '10k')
    display_scl.connect(pullup_scl[1])
    display_sda.connect(pullup_sda[1])
    media_3v3.connect(pullup_scl[2], pullup_sda[2], conn_display[2], conn_i2s[1])
    gnd.connect(conn_display[1], conn_i2s[2])

    catalog.series(
        'LED_BEAT',
        'R19',
        '1k',
        media['RX'],
        beat_led[1],
        side_a_name='LED_BEAT_MCU',
        side_b_name='LED_BEAT_LED_K',
    )
    master_3v3.connect(beat_led[2])

    catalog.direct('I2S_LRCK', media['GPIO1'], conn_i2s[5])
    catalog.direct('I2S_DAT', media['GPIO12'], conn_i2s[4])
    catalog.direct('I2S_BCLK', media['GPIO13'], conn_i2s[3])


@subcircuit
def led_outputs(
    catalog: SignalCatalog,
    gpu0: Part,
    gpu1: Part,
    gpu1_3v3: Net,
    vcc: Net,
    gnd: Net,
) -> None:
    """3.3 V to 5 V LED clock/data level shifting and connectors."""

    shifter = Part('74xx', '74AHCT125', ref='U6', tag='U6')
    decoupler = Part('Device', 'C', ref='C1', tag='C1', value='100n')
    conn_led0 = Part('Connector', 'Conn_01x03_Pin', ref='J3', tag='J3')
    conn_led1 = Part('Connector', 'Conn_01x03_Socket', ref='J4', tag='J4')

    vcc.connect(shifter['VCC'], decoupler[1])
    gnd.connect(shifter['GND'], decoupler[2], conn_led0[2], conn_led1[2])

    catalog.direct('LED0_3V3_CLK', gpu0['GPIO7'], shifter[2])
    catalog.direct('LED0_3V3_DAT', gpu0['GPIO8'], shifter[5])
    catalog.direct('LED1_3V3_CLK', gpu1['GPIO10'], shifter[12])
    catalog.direct('LED1_3V3_DAT', gpu1['GPIO13'], shifter[9])

    led_enable = catalog.direct(
        'LED_EN',
        gpu1['GPIO9'],
        shifter[1],
        shifter[4],
        shifter[10],
        shifter[13],
    )
    enable_pullup = resistor('R22', '10k')
    led_enable.connect(enable_pullup[1])
    gpu1_3v3.connect(enable_pullup[2])

    catalog.direct('LED0_5V_CLK', shifter[3], conn_led0[1], stub=False)
    catalog.direct('LED0_5V_DAT', shifter[6], conn_led0[3], stub=False)
    catalog.direct('LED1_5V_DAT', shifter[8], conn_led1[3], stub=False)
    catalog.direct('LED1_5V_CLK', shifter[11], conn_led1[1], stub=False)


def build_circuit() -> tuple[Circuit, SignalCatalog]:
    """Build the complete circuit once for every generated artifact."""

    circuit = Circuit(name=OUTPUT_BASENAME)
    catalog = SignalCatalog()

    with circuit:
        vin = make_net('VIN_24V', drive=POWER)
        vcc = make_net('VCC_5V', drive=POWER, stub=True)
        vusb = make_net('VUSB_5V', drive=POWER)
        gnd = make_net('GND', drive=POWER, stub=True)
        master_3v3 = make_net('MASTER_3V3', stub=True)
        media_3v3 = make_net('MEDIA_3V3', stub=True)
        gpu1_3v3 = make_net('GPU1_3V3', stub=True)
        power_3v3 = make_net('POWER_3V3', stub=True)

        master = master_controller(vcc, gnd, master_3v3, tag='master_controller')
        media = media_controller(vcc, gnd, media_3v3, tag='media_controller')
        gpu0, gpu1 = gpu_controllers(
            vcc,
            gnd,
            gpu1_3v3,
            tag='gpu_controllers',
        )
        power = power_controller(vcc, gnd, power_3v3, tag='power_controller')

        low_speed_spi(
            catalog,
            master,
            media,
            power,
            master_3v3,
            tag='low_speed_spi',
        )
        high_speed_spi(
            catalog,
            master,
            gpu0,
            gpu1,
            master_3v3,
            tag='high_speed_spi',
        )
        power_distribution(
            catalog,
            power,
            vin,
            vcc,
            vusb,
            gnd,
            power_3v3,
            tag='power_distribution',
        )
        rs485_interface(catalog, master, master_3v3, gnd, tag='rs485_interface')
        media_io(
            catalog,
            media,
            media_3v3,
            master_3v3,
            gnd,
            tag='media_io',
        )
        led_outputs(
            catalog,
            gpu0,
            gpu1,
            gpu1_3v3,
            vcc,
            gnd,
            tag='led_outputs',
        )

        circuit.NC.stub = True

    for part in circuit.parts:
        if not part.footprint:
            part.footprint = ':'

    return circuit, catalog


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '-o',
        '--output-dir',
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help='directory for generated artifacts (default: schematic/build)',
    )
    parser.add_argument('--no-pdf', action='store_true', help='skip the optional PDF export')
    return parser.parse_args()


def main() -> None:
    configure_skidl()
    args = parse_args()
    circuit, catalog = build_circuit()
    outputs = generate_outputs(
        circuit,
        catalog,
        args.output_dir.resolve(),
        source_name=Path(__file__).name,
        generate_pdf=not args.no_pdf,
    )
    for output in outputs:
        print(output)


if __name__ == '__main__':
    main()
