#!/usr/bin/env python3

from __future__ import annotations

import os
import logging
import json
import subprocess
from typing import TypedDict
from platformio.project.config import ProjectConfig
from sys import argv

if len(argv) == 2:
    pioenv = argv[1]
else:
    Import("env")
    pioenv = env["PIOENV"]

logging.basicConfig(
    level=os.getenv("PIO_LOGLEVEL_CUSTOM", "INFO").upper(),
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger(__name__)

eval_output = False


class NoHwidError(RuntimeError):
    """Raised when a device does not have a hardware ID."""


class IncompleteDeviceError(RuntimeError):
    """Raised when a device does not have all required fields."""


class PioDevice(TypedDict):
    port: str
    description: str
    hwid: str


class PioPorts(TypedDict):
    cu: str
    tty: str


class PioDeviceWithSerial:
    vendor_id: str
    product_id: str
    serial_number: str
    port: str
    description: str
    hwid: str
    location: str

    def __init__(self, device: PioDevice):
        self.port = device["port"]
        self.description = device["description"]
        self.hwid = device["hwid"]

        hwid_parts = self.hwid.split(" ")
        if len(hwid_parts) < 3:
            raise NoHwidError(
                f"Device {self.port!r} does not have a valid hardware ID: {self.hwid!r}"
            )
        log.debug(f"HWID parts: {hwid_parts}")
        for part in hwid_parts:
            if part.startswith("SER="):
                self.serial_number = part[4:]
            elif part.startswith("VID:PID="):
                vidpid = part[8:].split(":")
                if len(vidpid) != 2:
                    raise NoHwidError(
                        f"Device {self.port!r} has invalid VID:PID: {part!r}"
                    )
                self.vendor_id, self.product_id = vidpid
            elif part.startswith("LOCATION="):
                self.location = part[9:]

        if any(
            not hasattr(self, attr)
            for attr in ("vendor_id", "product_id", "serial_number", "location")
        ):
            raise IncompleteDeviceError(
                f"Device {self.port!r} is missing required fields: {self.hwid!r}"
            )


def _load_devices() -> list[PioDevice]:
    """Return devices from `pio device list --json-output`."""
    out = subprocess.run(
        ["pio", "device", "list", "--json-output"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    log.debug(f"Device list output: {out}")
    return json.loads(out)


def _pick_port_by_serial(devs: list[PioDevice], serial: str) -> PioPorts | None:
    """Pick /dev/cu.* for given USB Serial Number."""
    pio_devs: list[PioDeviceWithSerial] = []
    for d in devs:
        try:
            pio_devs.append(PioDeviceWithSerial(d))
        except NoHwidError as e:
            log.debug(f"Skipping device {d['port']!r}: {e}")
        except IncompleteDeviceError as e:
            log.debug(f"Skipping device {d['port']!r}: {e}")
    matches = [d for d in pio_devs if d.serial_number == serial]
    if not matches:
        return None
    if len(matches) > 1:
        raise RuntimeError(
            f"Multiple devices found with serial {serial!r}: "
            f"{json.dumps(matches, indent=2)}"
        )
    # Prefer cu.* on macOS
    cu = [d for d in matches if "/cu." in d.port]
    cu_port = cu[0].port
    tty_port = cu_port.replace("/cu.", "/tty.")
    log.info(f"Selected port {cu_port} for USB serial {serial!r}")
    return {"cu": cu_port, "tty": tty_port}


def _set_ports(ports: PioPorts) -> None:
    """Apply to current env."""
    if not eval_output:
        log.debug("Setting environment variables directly")
        os.environ["UPLOAD_PORT"] = ports["cu"]
        os.environ["ESPTOOL_PORT"] = ports["tty"]
        globals()["env"].Append(UPLOAD_PORT=ports["cu"])
    else:
        log.debug("Printing export commands for shell evaluation")
        print(f'export UPLOAD_PORT="{ports["cu"]}"')
        print(f'export ESPTOOL_PORT="{ports["tty"]}"')
    log.info(f"Set port to {ports['cu']}")


def main() -> None:
    if "env" not in globals():
        config = ProjectConfig()
    else:
        config = globals()["env"].GetProjectConfig()

    custom_usb_serials = config.get(f"env:{pioenv}", "custom_usb_serials")

    if not isinstance(custom_usb_serials, str):
        raise RuntimeError("`custom_usb_serial` must be a list of USB serial numbers")

    success = False

    devs = _load_devices()
    for serial in custom_usb_serials.split():
        serial = serial.strip()
        if not serial:
            continue
        port = _pick_port_by_serial(devs, serial)
        if port:
            _set_ports(port)
            success = True
            break

    if not success:
        raise RuntimeError(
            "No matching device found for any of the specified USB serials: "
            f"{custom_usb_serials!r}"
        )


try:
    globals()["Import"]("env")
except (ImportError, KeyError, NameError):
    eval_output = True

if (
    "COMMAND_LINE_TARGETS" not in globals()
    or "upload" in globals()["COMMAND_LINE_TARGETS"]
    or "uploadfs" in globals()["COMMAND_LINE_TARGETS"]
):
    main()
