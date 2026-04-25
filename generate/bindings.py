#!/usr/bin/env python3

from __future__ import annotations

from dataclasses import dataclass
import sys

from lib.fun import (
    Dependencies,
    CommonEntry,
    generate,
)


GROUP = "Bindings"
TOKEN = "BINDING"
ANNOTATION = "binding"


@dataclass(frozen=True)
class BindingEntry(CommonEntry):
    pass


def extract(node: dict, entry: CommonEntry) -> BindingEntry:
    kinds = set(node["kind"] for node in node.get("inner", []))
    print(f"Extracted {entry.qualified_name} with kinds: {kinds}")
    return BindingEntry(
        qualified_name=entry.qualified_name,
        include_path=entry.include_path,
    )


def render_support() -> str:
    return """#pragma once

namespace Totem::Generated::Bindings {

} // namespace Totem::Generated::Wire
"""


def render(entry: CommonEntry) -> str:
    # lines = [
    #     "#pragma once",
    #     "",
    #     '#include "Generated/Wire/Support.hpp"',
    #     f'#include "{wire_struct.include_path}"',
    #     "#include <tuple>",
    #     "",
    #     "namespace Totem::Generated::Wire {",
    #     "",
    #     f"template <> struct FieldList<{wire_struct.qualified_name}> {{",
    #     f"    using Type = {wire_struct.qualified_name};",
    # ]
    #
    # if wire_struct.fields:
    #     lines.extend(
    #         [
    #             "    static constexpr auto fields = std::make_tuple(",
    #             ",\n".join(
    #                 f'        Field<&Type::{field}>{{"{field}"}}'
    #                 for field in wire_struct.fields
    #             ),
    #             "    );",
    #         ]
    #     )
    # else:
    #     lines.append("    static constexpr auto fields = std::make_tuple();")
    #
    # lines.extend(["};", "", "} // namespace Totem::Generated::Wire", ""])
    # return "\n".join(lines)

    print(entry)
    return ""


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
