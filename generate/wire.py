#!/usr/bin/env python3

from __future__ import annotations

from dataclasses import dataclass
import sys

from lib.fun import (
    CommonEntry,
    Dependencies,
    generate,
)


GROUP = "Wire"
TOKEN = "WIRE_MSG"
ANNOTATION = "wire"


@dataclass(frozen=True)
class WireEntry(CommonEntry):
    fields: tuple[str, ...]


def extract(node: dict, entry: CommonEntry) -> WireEntry:
    fields = tuple(
        node["name"]
        for node in node.get("inner", [])
        if node.get("kind") == "FieldDecl" and node.get("name")
    )
    print(f"Extracted {entry.qualified_name} with fields: {fields}")
    return WireEntry(
        qualified_name=entry.qualified_name,
        include_path=entry.include_path,
        fields=fields,
    )


def render_support() -> str:
    return """#pragma once

namespace Totem::Generated::Wire {

template <auto MemberPtr> struct Field {
    static constexpr auto member = MemberPtr;
    const char *name;
};

template <typename T> struct FieldList;

} // namespace Totem::Generated::Wire
"""


def render(entry: WireEntry) -> str:
    lines = [
        "#pragma once",
        "",
        '#include "Generated/Wire/Support.hpp"',
        f'#include "{entry.include_path}"',
        "#include <tuple>",
        "",
        "namespace Totem::Generated::Wire {",
        "",
        f"template <> struct FieldList<{entry.qualified_name}> {{",
        f"    using Type = {entry.qualified_name};",
    ]

    print(entry)

    if entry.fields:
        lines.extend(
            [
                "    static constexpr auto fields = std::make_tuple(",
                ",\n".join(
                    f'        Field<&Type::{field}>{{"{field}"}}'
                    for field in entry.fields
                ),
                "    );",
            ]
        )
    else:
        lines.append("    static constexpr auto fields = std::make_tuple();")

    lines.extend(["};", "", "} // namespace Totem::Generated::Wire", ""])
    return "\n".join(lines)


deps = Dependencies(
    group=GROUP,
    token=TOKEN,
    annotation=ANNOTATION,
    render_support=render_support,
    render=render,
    extract=extract,
)

if __name__ == "__main__":
    sys.exit(generate(deps))
