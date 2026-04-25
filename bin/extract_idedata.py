#!/usr/bin/env python3

from __future__ import annotations

import json
import pathlib
import sys


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: extract_idedata.py <output>")

    output = pathlib.Path(sys.argv[1])
    text = sys.stdin.read()

    start = text.find('{"build_type":')
    end = text.rfind("}")
    if start == -1 or end == -1 or end < start:
        raise RuntimeError("Could not find idedata JSON in PlatformIO output")

    data = json.loads(text[start : end + 1])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n",
                      encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
