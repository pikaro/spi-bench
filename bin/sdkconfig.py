#!/usr/bin/env python3

from __future__ import annotations

import os
import logging
from sys import argv
import configparser
from pathlib import Path
import hashlib

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

parser = configparser.ConfigParser()
with Path("platformio.ini").open() as f:
    parser.read_file(f)

section = f"env:{pioenv}"
stack = []

if not parser.has_section(section):
    _error = f"Section {section} not found in platformio.ini"
    raise RuntimeError(_error)

sdkconfig_name = parser.get(
    section,
    "custom_sdkconfig",
    fallback=pioenv,
)

while section:
    if not parser.has_section(section):
        _error = f"Section {section} not found in platformio.ini"
        raise RuntimeError(_error)

    stack.append(section.replace("env:", ""))

    if parser.has_option(section, "extends"):
        section = parser.get(section, "extends")
    else:
        section = None

sdkconfig = f"sdkconfig.{sdkconfig_name}.defaults"
sdkconfig_tmp = f"{sdkconfig}.tmp"

with open(sdkconfig_tmp, "w") as f:
    for i, item in enumerate(reversed(stack)):
        if Path(f"sdkconfig.stack.{item}").exists():
            with Path(f"sdkconfig.stack.{item}").open() as stack_file:
                if i > 0:
                    f.write("\n")
                f.write(f"# {'=' * 80}\n")
                f.write(f"# = sdkconfig.stack.{item}\n")
                f.write(f"# {'=' * 80}\n\n")
                f.write(stack_file.read())
                log.info(f"Included sdkconfig.stack.{item} in {sdkconfig}")

if not Path(sdkconfig).exists():
    Path(sdkconfig_tmp).rename(sdkconfig)
else:
    md5_old = hashlib.md5(Path(sdkconfig).read_bytes()).hexdigest()
    md5_new = hashlib.md5(Path(sdkconfig_tmp).read_bytes()).hexdigest()

    if md5_old != md5_new:
        Path(sdkconfig_tmp).rename(sdkconfig)
        if Path(f"sdkconfig.{sdkconfig_name}").exists():
            Path(f"sdkconfig.{sdkconfig_name}").unlink()
        log.info(f"Updated {sdkconfig} with new content")
    else:
        Path(sdkconfig_tmp).unlink()
