#!/usr/bin/env python3

from __future__ import annotations

import argparse
import glob
import json
import os
import pathlib
import shlex
import subprocess
import sys
from dataclasses import dataclass


WORKSPACE_ROOT = pathlib.Path(__file__).resolve().parent.parent
WIRE_ANNOTATION = "wire"
WIRE_TOKEN = "WIRE_MSG"
RECORD_KINDS = {"CXXRecordDecl", "ClassTemplateSpecializationDecl"}
PIO_PACKAGES_DIR = pathlib.Path.home() / ".platformio" / "packages"
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx"}


@dataclass(frozen=True)
class WireStruct:
    qualified_name: str
    include_path: str
    fields: tuple[str, ...]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compdb", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--scan-root", action="append", default=["include"])
    return parser.parse_args()


def load_compdb(path: pathlib.Path) -> list[dict]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def compiler_args(entry: dict) -> list[str]:
    args = entry.get("arguments")
    if args is None:
        args = shlex.split(entry["command"])
    return args


def which_local(binary: str) -> str | None:
    for path_dir in os.environ.get("PATH", "").split(os.pathsep):
        if not path_dir:
            continue
        candidate = pathlib.Path(path_dir) / binary
        if candidate.exists() and os.access(candidate, os.X_OK):
            return str(candidate)
    return None


def resolve_compiler(binary: str) -> str:
    if os.path.isabs(binary) and os.access(binary, os.X_OK):
        return binary

    resolved = which_local(binary)
    if resolved is not None:
        return resolved

    binary_name = pathlib.Path(binary).name
    matches = [
        match
        for match in glob.glob(
            str(PIO_PACKAGES_DIR / "toolchain-*" / "bin" / binary_name)
        )
        if os.access(match, os.X_OK)
    ]
    if matches:
        matches.sort()
        return matches[0]

    raise FileNotFoundError(f"Could not resolve compiler binary: {binary}")


def resolve_clang_driver(source_file: str) -> str:
    binary = (
        "clang++"
        if pathlib.Path(source_file).suffix
        in {
            ".cc",
            ".cpp",
            ".cxx",
            ".hpp",
            ".hh",
            ".hxx",
        }
        else "clang"
    )
    return resolve_compiler(binary)


def host_sdk_flags() -> list[str]:
    if sys.platform != "darwin":
        return []

    result = subprocess.run(
        ["xcrun", "--show-sdk-path"],
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        return []

    sdk_path = result.stdout.strip()
    if not sdk_path:
        return []

    flags = ["-isysroot", sdk_path]
    cxx_include = pathlib.Path(sdk_path) / "usr" / "include" / "c++" / "v1"
    if cxx_include.exists():
        flags.extend(["-isystem", str(cxx_include)])
    return flags


def should_keep_arg(arg: str) -> bool:
    if not arg.startswith("-"):
        return False

    if arg.startswith(("-D", "-U", "-I", "-std=", "--std=")):
        return True

    if arg in {
        "-nostdinc",
        "-nostdinc++",
        "-undef",
        "-fshort-enums",
        "-fpack-struct",
        "-fno-exceptions",
        "-fexceptions",
        "-fno-rtti",
        "-frtti",
        "-funsigned-char",
        "-fsigned-char",
        "-Winvalid-offsetof",
        "-Wno-invalid-offsetof",
    }:
        return True

    return False


def workspace_cpp_entries(compdb: list[dict]) -> list[dict]:
    entries: list[dict] = []
    for entry in compdb:
        path = pathlib.Path(entry["file"]).resolve()
        try:
            rel = path.relative_to(WORKSPACE_ROOT)
        except ValueError:
            continue

        if rel.parts and rel.parts[0] == ".pio":
            continue

        if path.suffix not in {".cc", ".cpp", ".cxx"}:
            continue

        entries.append(entry)
    return entries


def base_parse_args(entry: dict) -> list[str]:
    args = compiler_args(entry)
    filtered: list[str] = [resolve_clang_driver(entry["file"])]
    filtered.extend(host_sdk_flags())
    filtered.append("-DNDEBUG")

    passthrough_with_value = {
        "-isystem",
        "-iquote",
        "-idirafter",
        "-include",
        "-imacros",
        "-x",
        "-isysroot",
        "--sysroot",
    }
    drop_with_value = {"-o", "-MF", "-MT", "-MQ", "-MJ"}

    i = 0
    while i < len(args):
        arg = args[i]
        if i == 0:
            i += 1
            continue
        if arg in drop_with_value:
            i += 2
            continue
        if arg in passthrough_with_value:
            if i + 1 >= len(args):
                raise RuntimeError(f"Missing value for compiler argument: {arg}")
            filtered.extend([arg, args[i + 1]])
            i += 2
            continue
        if arg == "-c":
            i += 1
            continue
        if should_keep_arg(arg):
            filtered.append(arg)
        i += 1

    return filtered


def find_wire_headers(scan_roots: list[str]) -> list[pathlib.Path]:
    headers: list[pathlib.Path] = []
    for root in scan_roots:
        base = (WORKSPACE_ROOT / root).resolve()
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if not path.is_file() or path.suffix not in HEADER_SUFFIXES:
                continue
            try:
                contents = path.read_text(encoding="utf-8")
            except UnicodeDecodeError:
                continue
            if WIRE_TOKEN in contents:
                headers.append(path)
    headers.sort()
    return headers


def ast_for_header(header: pathlib.Path, entry: dict) -> dict:
    cmd = base_parse_args(entry)
    cmd.extend(
        [
            "-x",
            "c++-header",
            "-Xclang",
            "-ast-dump=json",
            "-fsyntax-only",
            str(header),
        ]
    )
    result = subprocess.run(
        cmd,
        cwd=WORKSPACE_ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(f"AST dump failed for {header}:\n{result.stderr}")
    return json.loads(result.stdout)


def has_wire_annotation(node: dict) -> bool:
    for child in node.get("inner", []):
        if child.get("kind") == "AnnotateAttr":
            return True
    return False


def resolve_file(node: dict) -> str | None:
    loc = node.get("loc")
    if loc:
        filename = loc.get("file")
        if filename:
            return filename

    range_blob = node.get("range", {})
    begin = range_blob.get("begin", {})
    for candidate in (
        begin,
        begin.get("expansionLoc", {}),
        begin.get("spellingLoc", {}),
    ):
        filename = candidate.get("file")
        if filename:
            return filename

    for child in node.get("inner", []):
        filename = resolve_file(child)
        if filename:
            return filename
    return None


def workspace_include_path(filename: str) -> str | None:
    path = pathlib.Path(filename).resolve()
    try:
        rel = path.relative_to(WORKSPACE_ROOT / "include")
    except ValueError:
        return None
    return rel.as_posix()


def collect_wire_structs(
    node: dict, scope: list[str], out: dict[str, WireStruct]
) -> None:
    kind = node.get("kind")
    name = node.get("name")

    next_scope = scope
    if kind == "NamespaceDecl" and name:
        next_scope = [*scope, name]
    elif kind in RECORD_KINDS and name:
        next_scope = [*scope, name]
        if node.get("completeDefinition") and has_wire_annotation(node):
            include_path = workspace_include_path(resolve_file(node) or "")
            if include_path is not None:
                qualified_name = "::" + "::".join(next_scope)
                fields = tuple(
                    child["name"]
                    for child in node.get("inner", [])
                    if child.get("kind") == "FieldDecl" and child.get("name")
                )
                out.setdefault(
                    qualified_name,
                    WireStruct(
                        qualified_name=qualified_name,
                        include_path=include_path,
                        fields=fields,
                    ),
                )

    for child in node.get("inner", []):
        collect_wire_structs(child, next_scope, out)


def sanitize_filename(qualified_name: str) -> str:
    return qualified_name.strip(":").replace("::", "__") + ".hh"


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


def render_struct_header(wire_struct: WireStruct) -> str:
    lines = [
        "#pragma once",
        "",
        '#include "Generated/Wire/Support.hh"',
        f'#include "{wire_struct.include_path}"',
        "#include <tuple>",
        "",
        "namespace Totem::Generated::Wire {",
        "",
        f"template <> struct FieldList<{wire_struct.qualified_name}> {{",
        f"    using Type = {wire_struct.qualified_name};",
    ]

    if wire_struct.fields:
        lines.extend(
            [
                "    static constexpr auto fields = std::make_tuple(",
                ",\n".join(
                    f'        Field<&Type::{field}>{{"{field}"}}'
                    for field in wire_struct.fields
                ),
                "    );",
            ]
        )
    else:
        lines.append("    static constexpr auto fields = std::make_tuple();")

    lines.extend(["};", "", "} // namespace Totem::Generated::Wire", ""])
    return "\n".join(lines)


def render_all(structs: list[WireStruct]) -> str:
    lines = [
        "#pragma once",
        "",
        "// IWYU pragma: begin_exports",
        "",
        '#include "Generated/Wire/Support.hh"',
    ]
    for wire_struct in structs:
        lines.append(
            f'#include "Generated/Wire/{sanitize_filename(wire_struct.qualified_name)}"'
        )
    lines.append("")
    lines.append("// IWYU pragma: end_exports")
    return "\n".join(lines)


def write_file(path: pathlib.Path, contents: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents, encoding="ascii")


def main() -> int:
    args = parse_args()
    compdb = load_compdb(pathlib.Path(args.compdb))
    entries = workspace_cpp_entries(compdb)
    if not entries:
        raise RuntimeError("No workspace C++ compile commands found")

    headers = find_wire_headers(args.scan_root)
    out_dir = pathlib.Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    wire_structs: dict[str, WireStruct] = {}
    parse_entry = entries[0]
    for header in headers:
        ast = ast_for_header(header, parse_entry)
        collect_wire_structs(ast, [], wire_structs)

    structs = sorted(wire_structs.values(), key=lambda s: s.qualified_name)
    write_file(out_dir / "Support.hh", render_support())
    write_file(out_dir / "All.hh", render_all(structs))
    for wire_struct in structs:
        write_file(
            out_dir / sanitize_filename(wire_struct.qualified_name),
            render_struct_header(wire_struct),
        )

    return 0


if __name__ == "__main__":
    sys.exit(main())
