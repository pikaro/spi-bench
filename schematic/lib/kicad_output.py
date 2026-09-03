# ruff: noqa: C901, PLR2004

"""KiCad generation and narrowly-scoped SKiDL 2.3 output repairs."""

from __future__ import annotations

import math
import os
import re
import shutil
import subprocess
import uuid
from pathlib import Path
from typing import TYPE_CHECKING, Any

from skidl.skidl import KICAD10
from skidl.tools.inject_labels import parse_netlist, parse_sexp

from .skidl_helpers import LAYOUT_SEED, SignalCatalog

if TYPE_CHECKING:
    from skidl import Circuit

OUTPUT_BASENAME = 'perfboard-v2'
SCHEMATIC_FILENAME = f'{OUTPUT_BASENAME}.kicad_sch'
CONFIG_NETLIST_FILENAME = f'{OUTPUT_BASENAME}.config-netlist.json'
KICAD_NETLIST_FILENAME = f'{OUTPUT_BASENAME}.net'
PDF_FILENAME = f'{OUTPUT_BASENAME}.pdf'


def _sexp_child(node: list[Any], tag: str) -> list[Any] | None:
    for child in node:
        if isinstance(child, list) and child and child[0] == tag:
            return child
    return None


def _sexp_children(node: list[Any], tag: str) -> list[list[Any]]:
    return [child for child in node if isinstance(child, list) and child and child[0] == tag]


def _pin_definition(pin: list[Any]) -> dict[str, Any] | None:
    at = _sexp_child(pin, 'at')
    number = _sexp_child(pin, 'number')
    if at is None or number is None:
        return None
    return {
        'number': str(number[1]),
        'x': float(at[1]),
        'y': float(at[2]),
    }


def _library_pins_by_unit(tree: list[Any]) -> dict[tuple[str, int], list[dict[str, Any]]]:
    """Extract pin definitions without conflating a multi-unit part's units."""

    lib_symbols = _sexp_child(tree, 'lib_symbols')
    if lib_symbols is None:
        return {}

    result: dict[tuple[str, int], list[dict[str, Any]]] = {}
    unit_suffix = re.compile(r'_(\d+)_\d+$')
    for library_symbol in _sexp_children(lib_symbols, 'symbol'):
        library_id = str(library_symbol[1])
        for unit_symbol in _sexp_children(library_symbol, 'symbol'):
            match = unit_suffix.search(str(unit_symbol[1]))
            if match is None:
                continue
            unit = int(match.group(1))
            pins = [
                definition
                for pin in _sexp_children(unit_symbol, 'pin')
                if (definition := _pin_definition(pin)) is not None
            ]
            result.setdefault((library_id, unit), []).extend(pins)
    return result


def _schematic_instances(tree: list[Any]) -> list[dict[str, Any]]:
    instances: list[dict[str, Any]] = []
    for symbol in _sexp_children(tree, 'symbol'):
        library_id = _sexp_child(symbol, 'lib_id')
        at = _sexp_child(symbol, 'at')
        unit = _sexp_child(symbol, 'unit')
        if library_id is None or at is None or unit is None:
            continue

        reference = next(
            (
                str(prop[2])
                for prop in _sexp_children(symbol, 'property')
                if len(prop) >= 3 and prop[1] == 'Reference'
            ),
            None,
        )
        if reference is None:
            continue

        mirror = _sexp_child(symbol, 'mirror')
        mirror_axis = str(mirror[1]) if mirror is not None else ''
        instances.append(
            {
                'library_id': str(library_id[1]),
                'reference': reference,
                'unit': int(unit[1]),
                'x': float(at[1]),
                'y': float(at[2]),
                'rotation': float(at[3]) if len(at) > 3 else 0.0,
                'mirror_x': 'x' in mirror_axis,
                'mirror_y': 'y' in mirror_axis,
            },
        )
    return instances


def _pin_world_position(
    instance: dict[str, Any],
    pin: dict[str, Any],
) -> tuple[float, float]:
    pin_x = pin['x']
    pin_y = -pin['y']
    if instance['mirror_x']:
        pin_y = -pin_y
    if instance['mirror_y']:
        pin_x = -pin_x

    angle = math.radians(instance['rotation'])
    return (
        instance['x'] + pin_x * math.cos(angle) + pin_y * math.sin(angle),
        instance['y'] - pin_x * math.sin(angle) + pin_y * math.cos(angle),
    )


def _label_angle(instance: dict[str, Any], x: float, y: float) -> int:
    angle = math.degrees(math.atan2(-(y - instance['y']), x - instance['x'])) % 360
    return round(angle / 90) * 90 % 360


def _format_coordinate(value: float) -> str:
    if value == int(value):
        return str(int(value))
    return f'{value:.4f}'.rstrip('0').rstrip('.')


def _global_label(name: str, x: float, y: float, angle: int, identity: str) -> str:
    label_uuid = uuid.uuid5(uuid.NAMESPACE_URL, f'perfboard-v2:{identity}')
    justification = 'left' if angle in (0, 90) else 'right'
    return (
        f'  (global_label "{name}"\n'
        '    (shape input)\n'
        f'    (at {_format_coordinate(x)} {_format_coordinate(y)} {angle})\n'
        '    (effects\n'
        '      (font\n'
        '        (size 1.27 1.27))\n'
        f'      (justify {justification}))\n'
        f'    (uuid "{label_uuid}"))\n'
    )


def _wire(start_x: float, start_y: float, end_x: float, end_y: float, identity: str) -> str:
    wire_uuid = uuid.uuid5(uuid.NAMESPACE_URL, f'perfboard-v2:wire:{identity}')
    return (
        '  (wire\n'
        '    (pts\n'
        f'      (xy {_format_coordinate(start_x)} {_format_coordinate(start_y)})\n'
        f'      (xy {_format_coordinate(end_x)} {_format_coordinate(end_y)}))\n'
        '    (stroke\n'
        '      (width 0)\n'
        '      (type default))\n'
        f'    (uuid "{wire_uuid}"))\n'
    )


def _stub_end(instance: dict[str, Any], x: float, y: float) -> tuple[float, float]:
    delta_x = x - instance['x']
    delta_y = y - instance['y']
    if abs(delta_x) >= abs(delta_y):
        return x + math.copysign(2.54, delta_x), y
    return x, y + math.copysign(2.54, delta_y)


def _form_end(text: str, start: int) -> int:
    depth = 0
    quoted = False
    escaped = False
    for index in range(start, len(text)):
        character = text[index]
        if quoted:
            if escaped:
                escaped = False
            elif character == '\\':
                escaped = True
            elif character == '"':
                quoted = False
            continue
        if character == '"':
            quoted = True
        elif character == '(':
            depth += 1
        elif character == ')':
            depth -= 1
            if depth == 0:
                return index + 1
    message = 'Unterminated KiCad S-expression'
    raise ValueError(message)


def _remove_named_global_labels(text: str, names: set[str]) -> str:
    pattern = re.compile(r'^  \(global_label "([^"]+)"', re.MULTILINE)
    ranges = [
        (match.start(), _form_end(text, match.start()))
        for match in pattern.finditer(text)
        if match.group(1) in names
    ]
    for start, end in reversed(ranges):
        text = text[:start] + text[end:]
    return text


def normalize_embedded_custom_pin_numbers(schematic_file: Path) -> int:
    """Copy custom pin names into blank embedded KiCad pin-number fields."""

    replacements = 0
    schematic_files = [
        schematic_file,
        *sorted(schematic_file.parent.glob(f'{schematic_file.name}_*.kicad_sch')),
    ]
    custom_symbol_pattern = re.compile(r'^    \(symbol "Custom:[^"]+"', re.MULTILINE)
    pin_pattern = re.compile(r'\(pin [^\n]+')
    for child_file in schematic_files:
        text = child_file.read_text()
        custom_ranges = [
            (match.start(), _form_end(text, match.start()))
            for match in custom_symbol_pattern.finditer(text)
        ]
        for start, end in reversed(custom_ranges):
            symbol_text = text[start:end]
            pin_ranges = [
                (match.start(), _form_end(symbol_text, match.start()))
                for match in pin_pattern.finditer(symbol_text)
            ]
            for pin_start, pin_end in reversed(pin_ranges):
                pin_text = symbol_text[pin_start:pin_end]
                name = re.search(r'\(name "([^"]+)"', pin_text)
                number = re.search(r'\(number ""', pin_text)
                if name is None or number is None:
                    continue
                number_start = pin_start + number.start() + len('(number "')
                symbol_text = (
                    symbol_text[:number_start] + name.group(1) + symbol_text[number_start:]
                )
                replacements += 1
            text = text[:start] + symbol_text + text[end:]
        child_file.write_text(text)
    return replacements


def repair_multiunit_labels(schematic_file: Path, netlist_file: Path) -> int:
    """Repair SKiDL's malformed multi-unit output for the U6 package."""

    child_file = schematic_file.with_name(f'{schematic_file.name}_led_outputs.kicad_sch')
    text = child_file.read_text()
    text = re.sub(r'("74AHCT125_\d+)_\d+"', r'\1_1"', text)
    tree = parse_sexp(text)
    pin_to_net = parse_netlist(str(netlist_file))
    target_nets = {
        net_name for (reference, _pin), net_name in pin_to_net.items() if reference == 'U6'
    }
    if not target_nets:
        message = 'No U6 nets found while repairing multi-unit labels'
        raise ValueError(message)

    pins_by_unit = _library_pins_by_unit(tree)
    labels: list[str] = []
    placed: set[tuple[str, int, int]] = set()
    for instance in _schematic_instances(tree):
        pins = [
            *pins_by_unit.get((instance['library_id'], 0), ()),
            *pins_by_unit.get((instance['library_id'], instance['unit']), ()),
        ]
        for pin in pins:
            net_name = pin_to_net.get((instance['reference'], pin['number']))
            if net_name not in target_nets:
                continue
            if net_name == 'GND' and instance['reference'] != 'U6':
                continue
            x, y = _pin_world_position(instance, pin)
            position = (net_name, round(x * 1000), round(y * 1000))
            if position in placed:
                continue
            placed.add(position)
            identity = (
                f'{net_name}:{instance["reference"]}:{instance["unit"]}:'
                f'{pin["number"]}:{position[1]}:{position[2]}'
            )
            label_x, label_y = _stub_end(instance, x, y)
            labels.append(_wire(x, y, label_x, label_y, identity))
            labels.append(
                _global_label(
                    net_name,
                    label_x,
                    label_y,
                    _label_angle(instance, label_x, label_y),
                    identity,
                ),
            )

    text = _remove_named_global_labels(text, target_nets)
    closing_parenthesis = text.rfind(')')
    child_file.write_text(
        text[:closing_parenthesis] + '\n' + ''.join(labels) + text[closing_parenthesis:],
    )
    return len(labels)


def export_pdf(schematic_file: Path, pdf_file: Path) -> bool:
    """Export the hierarchical KiCad schematic as a multi-page PDF."""

    kicad_cli = shutil.which('kicad-cli')
    if kicad_cli is None:
        return False
    subprocess.run(  # noqa: S603
        [
            kicad_cli,
            'sch',
            'export',
            'pdf',
            '--black-and-white',
            '--output',
            str(pdf_file),
            str(schematic_file),
        ],
        check=True,
    )
    return True


def refresh_kicad_erc(schematic_file: Path) -> None:
    """Replace SKiDL's pre-repair ERC report with one for the final files."""

    kicad_cli = shutil.which('kicad-cli')
    if kicad_cli is None:
        return
    subprocess.run(  # noqa: S603
        [
            kicad_cli,
            'sch',
            'erc',
            '--output',
            f'{OUTPUT_BASENAME}-erc.rpt',
            '--severity-all',
            str(schematic_file),
        ],
        check=True,
    )


def generate_outputs(
    circuit: Circuit,
    catalog: SignalCatalog,
    output_dir: Path,
    *,
    source_name: str,
    generate_pdf: bool = True,
) -> list[Path]:
    """Validate the pin contract and generate every requested artifact."""

    output_dir.mkdir(parents=True, exist_ok=True)

    previous_cwd = Path.cwd()
    os.chdir(output_dir)
    try:
        circuit.ERC()
        circuit.generate_netlist(
            file_=KICAD_NETLIST_FILENAME,
            tool=KICAD10,
            do_backup=False,
        )
        catalog.write_configuration_netlist(
            circuit,
            Path(CONFIG_NETLIST_FILENAME),
            source_name=source_name,
        )
        circuit.generate_schematic(
            filepath='.',
            top_name=SCHEMATIC_FILENAME,
            title='Perfboard V2 Logic Board',
            flatness=0.0,
            retries=4,
            seed=LAYOUT_SEED,
            auto_stub=True,
            auto_stub_fanout=5,
            auto_stub_fallback='raise',
            label_clearance=True,
        )
        normalize_embedded_custom_pin_numbers(Path(SCHEMATIC_FILENAME))
        repair_multiunit_labels(Path(SCHEMATIC_FILENAME), Path(KICAD_NETLIST_FILENAME))
        refresh_kicad_erc(Path(SCHEMATIC_FILENAME))

        outputs = [
            output_dir / KICAD_NETLIST_FILENAME,
            output_dir / CONFIG_NETLIST_FILENAME,
            output_dir / SCHEMATIC_FILENAME,
        ]
        if generate_pdf and export_pdf(Path(SCHEMATIC_FILENAME), Path(PDF_FILENAME)):
            outputs.append(output_dir / PDF_FILENAME)
        return outputs
    finally:
        os.chdir(previous_cwd)
