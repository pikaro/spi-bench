#!/usr/bin/env python3

from skidl import (
    ERC,
    POWER,
    Circuit,
    Net,
    Part,
    generate_dot,
    generate_schematic,
    lib_search_paths,
    set_default_tool,
)
from skidl.skidl import KICAD10

set_default_tool(KICAD10)
lib_search_paths[KICAD10] = [
    '.',
    '/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols',
    '/Users/david.reis/src/dre/kicad/symbols',
]

# pyright: reportOperatorIssue=false, reportUnusedExpression=false

with Circuit() as circuit:
    NC = circuit.NC
    Vin = Net('Vin')
    Vcc = Net('Vcc')
    Vusb = Net('Vusb')
    GND = Net('GND')

    Vin.drive = POWER
    Vcc.drive = POWER
    Vusb.drive = POWER
    GND.drive = POWER

    master = Part(
        'Custom.kicad_sym',
        'Module_ESP32S3_Zero',
        ref='U1',
    )

    media = Part(
        'Custom.kicad_sym',
        'Module_ESP32S3_Zero',
        ref='U2',
    )

    gpu0 = Part(
        'Custom.kicad_sym',
        'Module_ESP32S3_Zero',
        ref='U3',
    )

    gpu1 = Part(
        'Custom.kicad_sym',
        'Module_ESP32S3_Zero',
        ref='U4',
    )

    power = Part(
        'Custom.kicad_sym',
        'Module_ESP32C3_Supermini',
        ref='U5',
    )

    buck = Part(
        'Custom.kicad_sym',
        'Module_Buck_LM2596',
        ref='U7',
    )

    sense_24v = Part(
        'Custom.kicad_sym',
        'Module_INA226',
        ref='U8',
    )

    sense_5v = Part(
        'Custom.kicad_sym',
        'Module_INA226',
        ref='U9',
    )

    hat_rs485 = Part(
        'Custom.kicad_sym',
        'Module_Hat_RS485',
        ref='U10',
    )

    shifter = Part(
        '74xx',
        '74AHCT125',
        ref='U6',
    )

    conn_i2s = Part(
        'Connector',
        'Conn_01x05_Socket',
        ref='J1',
    )

    conn_rs485 = Part(
        'Connector',
        'Conn_01x05_Socket',
        ref='J2',
    )

    conn_led0 = Part(
        'Connector',
        'Conn_01x03_Pin',
        ref='J3',
    )

    conn_led1 = Part(
        'Connector',
        'Conn_01x03_Socket',
        ref='J4',
    )

    conn_power_in = Part(
        'Connector',
        'Conn_01x03_Socket',
        ref='J5',
    )

    conn_power_out = Part(
        'Connector',
        'Conn_01x03_Socket',
        ref='J6',
    )

    conn_display = Part(
        'Connector',
        'Conn_01x04_Pin',
        ref='J7',
    )

    conn_usb = Part(
        'Connector',
        'Conn_01x04_Pin',
        ref='J8',
    )

    switch_power = Part(
        'Switch',
        'SW_SPDT',
        ref='SW1',
    )

    class SimpleComponents:
        r_counter: int
        led_counter: int
        nets: dict[str, Net]

        def __init__(self) -> None:
            self.r_counter = 0
            self.led_counter = 0
            self.nets = {}

        def r(self, value: str) -> Part:
            self.r_counter += 1
            return Part('Device', 'R', value=value, ref=f'R{self.r_counter}')

        def net(self, name: str) -> Net:
            if name not in self.nets:
                self.nets[name] = Net(name)
            return self.nets[name]

        def led(self, color: str) -> Part:
            self.led_counter += 1
            return Part('Device', 'LED', color=color, ref=f'LED{self.led_counter}')

    components = SimpleComponents()
    R = components.r
    N = components.net
    LED = components.led

    conn_power_in[1] & Vin & sense_24v['IN+']
    conn_power_in[3] & GND

    sense_24v['IN+'] & sense_24v['V+']
    sense_24v['V-'] & sense_24v['GND']
    sense_24v['GND'] & GND
    sense_24v['IN-'] & buck['IN+']
    master['3V3'] & sense_24v['VCC']

    sense_24v['IN-'] & conn_power_out[1]
    GND & conn_power_out[3]

    buck['IN-'] & GND
    buck['OUT+'] & sense_5v['IN+']
    buck['OUT-'] & GND

    sense_5v['IN+'] & sense_5v['V+']
    sense_5v['V-'] & sense_5v['GND']
    sense_5v['GND'] & GND
    sense_5v['IN-'] & switch_power['1']
    master['3V3'] & sense_5v['VCC']

    conn_usb[4] & switch_power['3'] & Vusb
    conn_usb[1] & GND
    switch_power['2'] & Vcc

    Vcc & master['5V']
    Vcc & media['5V']
    Vcc & gpu0['5V']
    Vcc & gpu1['5V']
    Vcc & power['5V']
    Vcc & shifter['VCC']

    GND & master['GND']
    GND & media['GND']
    GND & gpu0['GND']
    GND & gpu1['GND']
    GND & power['GND']
    GND & shifter['GND']
    GND & hat_rs485['GND']

    master['GPIO1'] & N('SPI0_MISO') & R('33') & media['GPIO11'] & power['GPIO4']
    master['GPIO2'] & N('SPI0_CLK') & R('33') & media['GPIO10'] & power['GPIO3']
    master['GPIO3'] & N('SPI0_MOSI') & R('33') & media['GPIO9'] & power['GPIO1']
    master['GPIO4'] & N('SPI0_CS_MEDIA') & R('33') & media['GPIO8']
    master['GPIO5'] & N('SPI0_ATTN_MEDIA') & R('1k') & media['GPIO7']
    master['GPIO6'] & N('SPI0_CS_POWER') & R('33') & power['GPIO0']
    master['GPIO9'] & N('SPI0_ATTN_POWER') & R('1k') & power['GPIO10']

    master['GPIO7'] & N('SPI1_ATTN_GPU0') & R('1k') & gpu0['GPIO2']
    master['GPIO8'] & N('SPI1_CS_GPU0') & R('33') & gpu0['GPIO1']
    master['GPIO10'] & N('STROBE') & R('1k') & gpu0['GPIO10'] & gpu1['GPIO4']
    master['GPIO11'] & N('SPI1_MOSI') & R('33') & gpu0['GPIO11'] & gpu1['GPIO3']
    master['GPIO12'] & N('SPI1_CLK') & R('33') & gpu0['GPIO12'] & gpu1['GPIO2']
    master['GPIO13'] & N('SPI1_MISO') & R('33') & gpu0['GPIO13'] & gpu1['GPIO1']
    master['RX'] & N('SPI1_CS_GPU1') & R('33') & gpu1['GPIO5']
    master['TX'] & N('SPI1_ATTN_GPU1') & R('1k') & gpu1['GPIO6']

    master['GPIO14'] & N('RS485_RX') & hat_rs485['RX']
    master['GPIO15'] & N('RS485_TX') & hat_rs485['TX']

    master['GPIO16'] & N('RS485_ATTN') & R('1k') & conn_rs485[2]
    hat_rs485['A+'] & conn_rs485[3]
    hat_rs485['B-'] & conn_rs485[4]
    GND & conn_rs485[1]
    master['3V3'] & hat_rs485['VCC']

    media['GPIO5'] & N('I2C_DISP_SCL') & conn_display[3]
    media['GPIO6'] & N('I2C_DISP_SDA') & conn_display[4]
    conn_display[3] & R('10k') & media['3V3']
    conn_display[4] & R('10k') & media['3V3']
    GND & conn_display[1]
    media['3V3'] & conn_display[2]

    media['RX'] & N('LED_BEAT') & R('1k') & LED('blue') & master['3V3']

    media['GPIO1'] & N('I2S_LRCK') & conn_i2s[5]
    media['GPIO12'] & N('I2S_DAT') & conn_i2s[4]
    media['GPIO13'] & N('I2S_BCLK') & conn_i2s[3]
    GND & conn_i2s[2]
    media['3V3'] & conn_i2s[1]

    power['GPIO7'] & N('I2C_POWER_SDA') & sense_24v['SDA'] & sense_5v['SDA']
    power['GPIO8'] & N('I2C_POWER_SCL') & sense_24v['SCL'] & sense_5v['SCL']
    power['GPIO7'] & R('10k') & power['3V3']
    power['GPIO8'] & R('10k') & power['3V3']

    gpu0['GPIO7'] & N('LED0_3V3_CLK') & shifter[2]
    gpu0['GPIO8'] & N('LED0_3V3_DAT') & shifter[5]

    gpu1['GPIO13'] & N('LED1_3V3_CLK') & shifter[9]
    gpu1['GPIO10'] & N('LED1_3V3_DAT') & shifter[12]
    gpu1['GPIO9'] & N('LED_EN') & shifter[1] & shifter[4] & shifter[10] & shifter[13]
    gpu1['GPIO9'] & R('10k') & gpu1['3V3']

    shifter[3] & conn_led0[1]
    shifter[6] & conn_led0[3]
    GND & conn_led0[2]

    shifter[8] & conn_led1[1]
    shifter[11] & conn_led1[3]
    GND & conn_led1[2]

    master['GPIO17'] & NC
    master['GPIO18'] & NC
    master['GPIO38'] & NC
    master['GPIO39'] & NC
    master['GPIO40'] & NC
    master['GPIO41'] & NC
    master['GPIO42'] & NC
    master['GPIO45'] & NC

    media['TX'] & NC
    media['GPIO14'] & NC
    media['GPIO15'] & NC
    media['GPIO16'] & NC
    media['GPIO17'] & NC
    media['GPIO18'] & NC
    media['GPIO38'] & NC
    media['GPIO39'] & NC
    media['GPIO40'] & NC
    media['GPIO41'] & NC
    media['GPIO42'] & NC
    media['GPIO45'] & NC

    gpu0['GPIO3'] & NC
    gpu0['GPIO4'] & NC
    gpu0['GPIO5'] & NC
    gpu0['GPIO6'] & NC
    gpu0['RX'] & NC
    gpu0['TX'] & NC
    gpu0['GPIO14'] & NC
    gpu0['GPIO15'] & NC
    gpu0['GPIO16'] & NC
    gpu0['GPIO17'] & NC
    gpu0['GPIO18'] & NC
    gpu0['GPIO38'] & NC
    gpu0['GPIO39'] & NC
    gpu0['GPIO40'] & NC
    gpu0['GPIO41'] & NC
    gpu0['GPIO42'] & NC
    gpu0['GPIO45'] & NC

    gpu1['GPIO7'] & NC
    gpu1['GPIO8'] & NC
    gpu1['GPIO11'] & NC
    gpu1['GPIO12'] & NC
    gpu1['RX'] & NC
    gpu1['TX'] & NC
    gpu1['GPIO14'] & NC
    gpu1['GPIO15'] & NC
    gpu1['GPIO16'] & NC
    gpu1['GPIO17'] & NC
    gpu1['GPIO18'] & NC
    gpu1['GPIO38'] & NC
    gpu1['GPIO39'] & NC
    gpu1['GPIO40'] & NC
    gpu1['GPIO41'] & NC
    gpu1['GPIO42'] & NC
    gpu1['GPIO45'] & NC

    circuit.ERC()
    circuit.generate_schematic()
