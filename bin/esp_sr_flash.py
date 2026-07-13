#!/usr/bin/env python3

from __future__ import annotations

import csv
from pathlib import Path

Import("env")


def _parse_size(value: str) -> int:
    normalized = value.strip().lower()
    multiplier = 1
    if normalized.endswith("k"):
        multiplier = 1024
        normalized = normalized[:-1]
    elif normalized.endswith("m"):
        multiplier = 1024 * 1024
        normalized = normalized[:-1]
    return int(normalized, 0) * multiplier


def _model_partition_offset(partitions_csv: Path) -> int:
    with partitions_csv.open(newline="") as handle:
        rows = csv.reader(
            line
            for line in handle
            if line.strip() and not line.lstrip().startswith("#")
        )
        for row in rows:
            if len(row) >= 5 and row[0].strip() == "model":
                offset = row[3].strip()
                if not offset:
                    raise RuntimeError(
                        "ESP-SR model partition must have an explicit offset"
                    )
                return _parse_size(offset)
    raise RuntimeError(f"No ESP-SR model partition in {partitions_csv}")


if env.subst("$PIOENV") == "ai":
    project_dir = Path(env.subst("$PROJECT_DIR"))
    build_dir = Path(env.subst("$BUILD_DIR"))
    partitions_value = env.GetProjectOption("board_build.partitions")
    partitions_csv = Path(partitions_value)
    if not partitions_csv.is_absolute():
        partitions_csv = project_dir / partitions_csv

    offset = _model_partition_offset(partitions_csv)
    image = build_dir / "srmodels" / "srmodels.bin"

    # Upload commands snapshot FLASH_EXTRA_IMAGES before post scripts run.
    # Register the model image here; the post script supplies its build rule.
    env.Append(FLASH_EXTRA_IMAGES=[(hex(offset), str(image))])
