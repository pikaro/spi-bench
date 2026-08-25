# ruff: noqa: CPY001

"""Pin-level safety contract for the perfboard-v2 wiring.

The endpoint groups below are a literal transcription of every connected pin
expression in ``perfboard-v2.py`` at commit
``889f1d34f39b958bee5ad95f411418d618217a08``. Net names are intentionally not
part of the comparison. The contract checks only exact component references,
pin identifiers, passive orientation, component identities, and values.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Iterable

    from skidl import Circuit, Part

BASELINE_COMMIT = '889f1d34f39b958bee5ad95f411418d618217a08'


def _group(*endpoints: str) -> frozenset[tuple[str, str]]:
    return frozenset(tuple(endpoint.split('.', maxsplit=1)) for endpoint in endpoints)


EXPECTED_PIN_GROUPS = {
    'GND': _group(
        'J1.2',
        'J2.1',
        'J3.2',
        'J4.2',
        'J5.3',
        'J6.3',
        'J7.1',
        'J8.1',
        'U1.GND',
        'U2.GND',
        'U3.GND',
        'U4.GND',
        'U5.GND',
        'U6.7',
        'U7.IN-',
        'U7.OUT-',
        'U8.GND',
        'U8.V-',
        'U9.GND',
        'U9.V-',
        'U10.GND',
    ),
    'VIN_24V': _group('J5.1', 'U8.IN+', 'U8.V+'),
    'VIN_24V_SENSED': _group('J6.1', 'U7.IN+', 'U8.IN-'),
    'VCC_5V_RAW': _group('U7.OUT+', 'U9.IN+', 'U9.V+'),
    'VCC_5V_SENSED': _group('SW1.1', 'U9.IN-'),
    'VUSB_5V': _group('J8.4', 'SW1.3'),
    'VCC_5V': _group('SW1.2', 'U1.5V', 'U2.5V', 'U3.5V', 'U4.5V', 'U5.5V', 'U6.14'),
    'MASTER_3V3': _group('LED1.2', 'U1.3V3', 'U8.VCC', 'U9.VCC', 'U10.VCC'),
    'MEDIA_3V3': _group('J1.1', 'J7.2', 'R17.2', 'R18.2', 'U2.3V3'),
    'GPU1_3V3': _group('R22.2', 'U4.3V3'),
    'POWER_3V3': _group('R20.2', 'R21.2', 'U5.3V3'),
    'MASTER_SPI0_MISO': _group('R1.1', 'U1.GPIO1'),
    'SPI0_MISO': _group('R1.2', 'U2.GPIO11', 'U5.GPIO4'),
    'MASTER_SPI0_CLK': _group('R2.1', 'U1.GPIO2'),
    'SPI0_CLK': _group('R2.2', 'U2.GPIO10', 'U5.GPIO3'),
    'MASTER_SPI0_MOSI': _group('R3.1', 'U1.GPIO3'),
    'SPI0_MOSI': _group('R3.2', 'U2.GPIO9', 'U5.GPIO1'),
    'MASTER_SPI0_CS_MEDIA': _group('R4.1', 'U1.GPIO4'),
    'SPI0_CS_MEDIA': _group('R4.2', 'U2.GPIO8'),
    'MASTER_SPI0_ATTN_MEDIA': _group('R5.1', 'U1.GPIO5'),
    'SPI0_ATTN_MEDIA': _group('R5.2', 'U2.GPIO7'),
    'MASTER_SPI0_CS_POWER': _group('R6.1', 'U1.GPIO6'),
    'SPI0_CS_POWER': _group('R6.2', 'U5.GPIO0'),
    'MASTER_SPI0_ATTN_POWER': _group('R7.1', 'U1.GPIO9'),
    'SPI0_ATTN_POWER': _group('R7.2', 'U5.GPIO10'),
    'MASTER_SPI1_ATTN_GPU0': _group('R8.1', 'U1.GPIO7'),
    'SPI1_ATTN_GPU0': _group('R8.2', 'U3.GPIO2'),
    'MASTER_SPI1_CS_GPU0': _group('R9.1', 'U1.GPIO8'),
    'SPI1_CS_GPU0': _group('R9.2', 'U3.GPIO1'),
    'MASTER_STROBE': _group('R10.1', 'U1.GPIO10'),
    'STROBE': _group('R10.2', 'U3.GPIO10', 'U4.GPIO4'),
    'MASTER_SPI1_MOSI': _group('R11.1', 'U1.GPIO11'),
    'SPI1_MOSI': _group('R11.2', 'U3.GPIO11', 'U4.GPIO3'),
    'MASTER_SPI1_CLK': _group('R12.1', 'U1.GPIO12'),
    'SPI1_CLK': _group('R12.2', 'U3.GPIO12', 'U4.GPIO2'),
    'MASTER_SPI1_MISO': _group('R13.1', 'U1.GPIO13'),
    'SPI1_MISO': _group('R13.2', 'U3.GPIO13', 'U4.GPIO1'),
    'MASTER_SPI1_CS_GPU1': _group('R14.1', 'U1.RX'),
    'SPI1_CS_GPU1': _group('R14.2', 'U4.GPIO5'),
    'MASTER_SPI1_ATTN_GPU1': _group('R15.1', 'U1.TX'),
    'SPI1_ATTN_GPU1': _group('R15.2', 'U4.GPIO6'),
    'RS485_RX': _group('U1.GPIO14', 'U10.RX'),
    'RS485_TX': _group('U1.GPIO15', 'U10.TX'),
    'MASTER_RS485_ATTN': _group('R16.1', 'U1.GPIO16'),
    'RS485_ATTN': _group('J2.2', 'R16.2'),
    'RS485_A': _group('J2.3', 'U10.A+'),
    'RS485_B': _group('J2.4', 'U10.B-'),
    'I2C_DISP_SCL': _group('J7.3', 'R17.1', 'U2.GPIO5'),
    'I2C_DISP_SDA': _group('J7.4', 'R18.1', 'U2.GPIO6'),
    'LED_BEAT_MCU': _group('R19.1', 'U2.RX'),
    'LED_BEAT_LED_K': _group('LED1.1', 'R19.2'),
    'I2S_LRCK': _group('J1.5', 'U2.GPIO1'),
    'I2S_DAT': _group('J1.4', 'U2.GPIO12'),
    'I2S_BCLK': _group('J1.3', 'U2.GPIO13'),
    'I2C_POWER_SDA': _group('R20.1', 'U5.GPIO7', 'U8.SDA', 'U9.SDA'),
    'I2C_POWER_SCL': _group('R21.1', 'U5.GPIO8', 'U8.SCL', 'U9.SCL'),
    'LED0_3V3_CLK': _group('U3.GPIO7', 'U6.2'),
    'LED0_3V3_DAT': _group('U3.GPIO8', 'U6.5'),
    'LED1_3V3_CLK': _group('U4.GPIO13', 'U6.9'),
    'LED1_3V3_DAT': _group('U4.GPIO10', 'U6.12'),
    'LED_EN': _group('R22.1', 'U4.GPIO9', 'U6.1', 'U6.4', 'U6.10', 'U6.13'),
    'LED0_5V_CLK': _group('J3.1', 'U6.3'),
    'LED0_5V_DAT': _group('J3.3', 'U6.6'),
    'LED1_5V_CLK': _group('J4.1', 'U6.8'),
    'LED1_5V_DAT': _group('J4.3', 'U6.11'),
}

EXPECTED_VALUES = {
    'R1': '33',
    'R2': '33',
    'R3': '33',
    'R4': '33',
    'R5': '1k',
    'R6': '33',
    'R7': '1k',
    'R8': '1k',
    'R9': '33',
    'R10': '1k',
    'R11': '33',
    'R12': '33',
    'R13': '33',
    'R14': '33',
    'R15': '1k',
    'R16': '1k',
    'R17': '10k',
    'R18': '10k',
    'R19': '1k',
    'R20': '10k',
    'R21': '10k',
    'R22': '10k',
    'LED1': 'Blue',
}

EXPECTED_PART_NAMES = {
    **{f'R{number}': 'R' for number in range(1, 23)},
    'J1': 'Conn_01x05_Socket',
    'J2': 'Conn_01x05_Socket',
    'J3': 'Conn_01x03_Pin',
    'J4': 'Conn_01x03_Socket',
    'J5': 'Conn_01x03_Socket',
    'J6': 'Conn_01x03_Socket',
    'J7': 'Conn_01x04_Pin',
    'J8': 'Conn_01x04_Pin',
    'LED1': 'LED',
    'SW1': 'SW_SPDT',
    'U1': 'Module_ESP32S3_Zero',
    'U2': 'Module_ESP32S3_Zero',
    'U3': 'Module_ESP32S3_Zero',
    'U4': 'Module_ESP32S3_Zero',
    'U5': 'Module_ESP32C3_Supermini',
    'U6': '74AHCT125',
    'U7': 'Module_Buck_LM2596',
    'U8': 'Module_INA226',
    'U9': 'Module_INA226',
    'U10': 'Module_Hat_RS485',
}


def _actual_pin_groups(circuit: Circuit) -> set[frozenset[tuple[str, str]]]:
    groups = set()
    for net in circuit.nets:
        if net is circuit.NC or not net.valid or not net.pins:
            continue
        endpoints = frozenset(
            (pin.part.ref, str(pin.num)) for pin in net.pins if not pin.part.ref.startswith('#FLG')
        )
        if endpoints:
            groups.add(endpoints)
    return groups


def _render_groups(groups: Iterable[frozenset[tuple[str, str]]]) -> str:
    return '; '.join(
        ', '.join(f'{reference}.{pin}' for reference, pin in sorted(group))
        for group in sorted(groups, key=sorted)
    )


def _parts_by_reference(circuit: Circuit) -> dict[str, Part]:
    return {part.ref: part for part in circuit.parts if not part.ref.startswith('#')}


def _assert_part_contract(parts: dict[str, Part]) -> None:
    actual_part_names = {reference: part.name for reference, part in parts.items()}
    if actual_part_names != EXPECTED_PART_NAMES:
        message = (
            'Component identities differ from the committed baseline: '
            f'expected {EXPECTED_PART_NAMES}, got {actual_part_names}'
        )
        raise ValueError(message)


def _assert_value_contract(parts: dict[str, Part]) -> None:
    actual_values = {
        reference: parts[reference].value_to_str() if reference in parts else '<missing>'
        for reference in EXPECTED_VALUES
    }
    changed_values = {
        reference: (expected, actual_values[reference])
        for reference, expected in EXPECTED_VALUES.items()
        if actual_values[reference] != expected
    }
    if changed_values:
        message = f'Component values differ from the committed baseline: {changed_values}'
        raise ValueError(message)


def assert_pin_contract(circuit: Circuit) -> None:
    """Reject any pin connection, component identity, or value drift."""

    expected_groups = set(EXPECTED_PIN_GROUPS.values())
    actual_groups = _actual_pin_groups(circuit)
    missing = expected_groups - actual_groups
    added = actual_groups - expected_groups
    if missing or added:
        details = []
        if missing:
            details.append(f'missing groups: {_render_groups(missing)}')
        if added:
            details.append(f'added groups: {_render_groups(added)}')
        message = 'Pin connectivity differs from the committed baseline; ' + ' | '.join(details)
        raise ValueError(message)

    parts = _parts_by_reference(circuit)
    _assert_part_contract(parts)
    _assert_value_contract(parts)
