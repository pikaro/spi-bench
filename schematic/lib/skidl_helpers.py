# ruff: noqa: ANN401, C901, CPY001, PLR0913

"""Small SKiDL helpers used by the readable board description."""

from __future__ import annotations

import json
import random
import tempfile
from pathlib import Path
from typing import Any

import skidl
from skidl import Circuit, Net, Part, lib_search_paths, set_default_tool
from skidl.logger import stop_log_file_output
from skidl.skidl import KICAD10

LAYOUT_SEED = 0

KICAD_SYMBOL_DIR = Path('/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols')
CUSTOM_SYMBOL_DIR = Path('/Users/david.reis/src/dre/kicad/symbols')


def configure_skidl() -> None:
    """Select KiCad 10 and the symbol libraries used by this board."""

    # SKiDL creates ``<script>.log`` and ``<script>.erc`` at import time in the
    # caller's working directory. The final KiCad ERC report supersedes both.
    stop_log_file_output()
    random.seed(LAYOUT_SEED)
    set_default_tool(KICAD10)
    pickle_dir = Path(tempfile.gettempdir()) / 'skidl-perfboard-v2-cache'
    pickle_dir.mkdir(parents=True, exist_ok=True)
    skidl.config.pickle_dir = str(pickle_dir)
    lib_search_paths[KICAD10] = [
        str(Path.cwd()),
        str(KICAD_SYMBOL_DIR),
        str(CUSTOM_SYMBOL_DIR),
    ]


def custom_part(symbol: str, ref: str) -> Part:
    """Create a custom logical-module part with stable, unique pin IDs."""

    part = Part('Custom.kicad_sym', symbol, ref=ref, tag=ref)
    for pin in part:
        if not pin.num:
            pin.num = pin.name
    return part


def resistor(ref: str, value: str) -> Part:
    """Create a resistor whose reference is stable across code reordering."""

    return Part('Device', 'R', ref=ref, tag=ref, value=value)


def make_net(
    name: str,
    *pins: Any,
    drive: Any | None = None,
    stub: bool = False,
) -> Net:
    """Create a named electrical net and explicitly attach its pins."""

    net = Net(name)
    if drive is not None:
        net.drive = drive
    net.stub = stub
    net.connect(*pins)
    return net


def mark_no_connect(part: Part, *pin_names: str) -> None:
    """Mark a documented list of module pins as intentionally unused."""

    part.circuit.NC.connect(*(part[pin_name] for pin_name in pin_names))


class SignalCatalog:
    """Record logical signals while building their electrical implementation."""

    def __init__(self) -> None:
        self._signals: dict[str, dict[str, Any]] = {}

    def direct(self, name: str, *pins: Any, stub: bool = True) -> Net:
        """Create and record a logical signal implemented by one net."""

        net = make_net(name, *pins, stub=stub)
        self._record(name, pins, (net,), ())
        return net

    def series(
        self,
        name: str,
        resistor_ref: str,
        resistor_value: str,
        side_a_pin: Any,
        *side_b_pins: Any,
        side_a_name: str | None = None,
        side_b_name: str | None = None,
    ) -> tuple[Net, Net, Part]:
        """Record a signal with a series resistor after its source pin.

        The usual master-side and shared-bus net names are derived from the
        logical signal name. Exceptional names can still be supplied without
        cluttering the board description.
        """

        series_resistor = resistor(resistor_ref, resistor_value)
        side_a_name = side_a_name or f'MASTER_{name}'
        side_b_name = side_b_name or name
        side_a = make_net(side_a_name, side_a_pin, series_resistor[1], stub=True)
        side_b = make_net(side_b_name, series_resistor[2], *side_b_pins, stub=True)
        self._record(
            name,
            (side_a_pin, *side_b_pins),
            (side_a, side_b),
            (series_resistor,),
        )
        return side_a, side_b, series_resistor

    def _record(
        self,
        name: str,
        endpoints: tuple[Any, ...],
        electrical_nets: tuple[Net, ...],
        series_components: tuple[Part, ...],
    ) -> None:
        if name in self._signals:
            message = f'Logical signal {name!r} is already defined'
            raise ValueError(message)
        self._signals[name] = {
            'endpoints': endpoints,
            'electrical_nets': electrical_nets,
            'series_components': series_components,
        }

    @staticmethod
    def _endpoint(pin: Any) -> dict[str, Any]:
        part = pin.part
        return {
            'node': getattr(part, 'configuration_node', None),
            'ref': part.ref,
            'part': part.name,
            'sheet': '.'.join(part.hiertuple[1:]),
            'pin': pin.name,
            'pin_id': pin.num,
            'electrical_net': pin.net.name,
        }

    def write_configuration_netlist(
        self,
        circuit: Circuit,
        output_file: Path,
        *,
        source_name: str,
    ) -> None:
        """Write a deterministic logical and electrical JSON netlist."""

        logical_signals: dict[str, Any] = {}
        for name, signal in sorted(self._signals.items()):
            logical_signals[name] = {
                'electrical_nets': [net.name for net in signal['electrical_nets']],
                'series_components': [
                    {'ref': part.ref, 'value': part.value_to_str()}
                    for part in signal['series_components']
                ],
                'endpoints': [
                    self._endpoint(pin)
                    for pin in sorted(
                        signal['endpoints'],
                        key=lambda pin: (pin.part.ref, pin.name),
                    )
                ],
            }

        nodes: dict[str, Any] = {}
        for part in sorted(circuit.parts, key=lambda item: item.ref):
            node_name = getattr(part, 'configuration_node', None)
            if node_name is None:
                continue

            configured_pins: dict[str, Any] = {}
            for signal_name, signal in logical_signals.items():
                for endpoint in signal['endpoints']:
                    if endpoint['ref'] == part.ref:
                        configured_pins[endpoint['pin']] = {
                            'signal': signal_name,
                            'electrical_net': endpoint['electrical_net'],
                        }

            nodes[node_name] = {
                'ref': part.ref,
                'part': part.name,
                'sheet': '.'.join(part.hiertuple[1:]),
                'pins': dict(sorted(configured_pins.items())),
            }

        electrical_nets: dict[str, Any] = {}
        for net in sorted(circuit.nets, key=lambda item: item.name):
            if net is circuit.NC or not net.valid or not net.pins:
                continue
            electrical_nets[net.name] = [
                {
                    'ref': pin.part.ref,
                    'part': pin.part.name,
                    'pin': pin.name,
                    'pin_id': pin.num,
                    'pin_type': pin.get_pin_info()[2],
                }
                for pin in sorted(net.pins, key=lambda item: (item.part.ref, item.name))
            ]

        output = {
            'format': 'perfboard-config-netlist-v1',
            'source': source_name,
            'nodes': dict(sorted(nodes.items())),
            'logical_signals': logical_signals,
            'electrical_nets': electrical_nets,
        }
        output_file.write_text(json.dumps(output, indent=2, sort_keys=True) + '\n')
