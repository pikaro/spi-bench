from __future__ import annotations

import argparse
import re
from pathlib import Path

from .state import Config


def parse_duration(value: str) -> float:
    suffixes = {
        "ms": 0.001,
        "s": 1.0,
        "m": 60.0,
    }
    raw_value = value
    value = value.strip().lower()
    multiplier = 1.0

    for suffix, suffix_multiplier in suffixes.items():
        if value.endswith(suffix):
            value = value[: -len(suffix)]
            multiplier = suffix_multiplier
            break

    try:
        duration = float(value) * multiplier
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid duration: {raw_value!r}") from exc

    if duration <= 0:
        raise argparse.ArgumentTypeError("duration must be greater than zero")

    return duration


def parse_regex(value: str) -> re.Pattern[str]:
    try:
        return re.compile(value)
    except re.error as exc:
        raise argparse.ArgumentTypeError(
            f"invalid regular expression {value!r}: {exc}"
        ) from exc


def parse_positive_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid integer: {value!r}") from exc

    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be greater than zero")

    return parsed


def parse_non_negative_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid integer: {value!r}") from exc

    if parsed < 0:
        raise argparse.ArgumentTypeError("value must be zero or greater")

    return parsed


def parse_baud(value: str) -> str:
    if value in {"h", "l"} or value.isdigit():
        return value

    raise argparse.ArgumentTypeError("baud must be 'h', 'l', or numeric")


def parse_args(argv: list[str]) -> Config:
    parser = argparse.ArgumentParser(usage="%(prog)s [options] <env...>")
    parser.add_argument(
        "-b",
        "--baud",
        type=parse_baud,
        help="monitor baud rate; accepts h, l, or a numeric baud",
    )
    parser.add_argument(
        "-t",
        "--timeout",
        type=parse_duration,
        help="stop after the given duration; supports ms, s, and m suffixes",
    )
    parser.add_argument(
        "--reset",
        action="store_true",
        help="run pio reset for owned boards before opening monitors",
    )
    parser.add_argument(
        "--upload",
        action="store_true",
        help="run pio upload for owned boards before opening monitors",
    )
    parser.add_argument(
        "--uploadfs",
        action="store_true",
        help="run pio uploadfs for owned boards before opening monitors",
    )
    parser.add_argument(
        "--strip-ansi",
        action="store_true",
        help="remove ANSI escape sequences from monitor output",
    )
    parser.add_argument(
        "--include",
        action="append",
        default=[],
        type=parse_regex,
        metavar="REGEX",
        help="print only monitor lines matching REGEX; may be repeated",
    )
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        type=parse_regex,
        metavar="REGEX",
        help="suppress monitor lines matching REGEX; may be repeated",
    )
    parser.add_argument(
        "--before",
        type=parse_non_negative_int,
        default=0,
        metavar="N",
        help="print N lines before each matching monitor line",
    )
    parser.add_argument(
        "--after",
        type=parse_non_negative_int,
        default=0,
        metavar="N",
        help="print N lines after each matching monitor line",
    )
    parser.add_argument(
        "--context",
        type=parse_non_negative_int,
        metavar="N",
        help="print N lines before and after each matching monitor line",
    )
    parser.add_argument(
        "--summary",
        action="store_true",
        help="print the first matching line and summarize repeats at exit",
    )
    parser.add_argument(
        "--max-lines",
        type=parse_positive_int,
        help="stop after printing this many monitor lines",
    )
    parser.add_argument(
        "--log-file",
        type=Path,
        help="append the full monitor stream to PATH before filtering",
    )
    parser.add_argument("envs", nargs="+", metavar="env")
    parsed = parser.parse_args(argv)

    if parsed.envs[-1] in {"h", "l"} or parsed.envs[-1].isdigit():
        parser.error("baud must be passed with --baud")

    if len(set(parsed.envs)) != len(parsed.envs):
        parser.error("duplicate environments are not supported")

    before = parsed.before
    after = parsed.after
    if parsed.context is not None:
        before = parsed.context
        after = parsed.context

    return Config(
        envs=parsed.envs,
        baud=parsed.baud,
        reset=parsed.reset,
        upload=parsed.upload,
        uploadfs=parsed.uploadfs,
        timeout=parsed.timeout,
        strip_ansi=parsed.strip_ansi,
        include=parsed.include,
        exclude=parsed.exclude,
        before=before,
        after=after,
        summary=parsed.summary,
        max_lines=parsed.max_lines,
        log_file=parsed.log_file,
    )
