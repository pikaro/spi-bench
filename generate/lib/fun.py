#!/usr/bin/env python3

from __future__ import annotations

import argparse
from dataclasses import dataclass
import glob
import json
import os
import pathlib
import re
import shlex
import subprocess
import sys
from typing import Callable, TypeVar

WORKSPACE_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
PIO_PACKAGES_DIR = pathlib.Path.home() / ".platformio" / "packages"
HEADER_SUFFIXES = {".h", ".hpp", ".hxx"}
RECORD_KINDS = {"CXXRecordDecl", "ClassTemplateSpecializationDecl"}
CLANG_UNSUPPORTED_FLAGS = {
    "-fdiagnostics-format=sarif-file",
    "-fno-shrink-wrap",
    "-fno-tree-switch-conversion",
    "-fstrict-volatile-bitfields",
    "-mlongcalls",
}

EntryT = TypeVar("EntryT")


@dataclass(frozen=True)
class CommonEntry:
    qualified_name: str
    include_path: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compdb", required=True)
    parser.add_argument("--envdata")
    parser.add_argument("--out", required=True)
    parser.add_argument("--scan-root", action="append", default=["include"])
    return parser.parse_args()


def load_compdb(path: pathlib.Path) -> list[dict]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def load_envdata(path: pathlib.Path) -> dict:
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
            ".hpp",
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

    if arg in CLANG_UNSUPPORTED_FLAGS:
        return False

    return True


def should_keep_env_flag(arg: str) -> bool:
    if arg in CLANG_UNSUPPORTED_FLAGS:
        return False
    return arg.startswith("-")


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


def infer_target_from_envdata(envdata: dict) -> str:
    toolchain_includes = envdata.get("includes", {}).get("toolchain", [])
    for include in toolchain_includes:
        match = re.search(r"/lib/gcc/([^/]+)/", include)
        if match:
            return match.group(1)

    compiler = pathlib.Path(envdata["cxx_path"]).name
    if compiler.endswith("-g++"):
        return compiler.removesuffix("-g++")

    raise RuntimeError("Could not infer target triple from envdata")


def infer_sysroot_from_envdata(envdata: dict) -> pathlib.Path | None:
    toolchain_root = pathlib.Path(envdata["cxx_path"]).resolve().parent.parent
    target = infer_target_from_envdata(envdata)
    candidate = toolchain_root / target
    if candidate.exists():
        return candidate
    return None


def env_parse_args(envdata: dict) -> list[str]:
    filtered = [resolve_compiler("clang++")]
    filtered.append(f"--target={infer_target_from_envdata(envdata)}")

    if (sysroot := infer_sysroot_from_envdata(envdata)) is not None:
        filtered.append(f"--sysroot={sysroot}")

    for define in envdata.get("defines", []):
        filtered.append(f"-D{define}")

    include_sets = envdata.get("includes", {})
    for include in include_sets.get("build", []):
        filtered.append(f"-I{include}")
    for include in toolchain_include_dirs(envdata):
        filtered.append(f"-isystem{include}")

    for flag in envdata.get("cxx_flags", []):
        if should_keep_env_flag(flag):
            filtered.append(flag)

    return filtered


def toolchain_include_dirs(envdata: dict) -> list[str]:
    std_flag = next(
        (flag for flag in envdata.get("cxx_flags", []) if flag.startswith("-std=")),
        "-std=gnu++23",
    )
    result = subprocess.run(
        [
            envdata["cxx_path"],
            std_flag,
            "-E",
            "-x",
            "c++",
            "-",
            "-v",
        ],
        cwd=WORKSPACE_ROOT,
        text=True,
        capture_output=True,
        input="",
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"Could not query toolchain include directories:\n{result.stderr}"
        )

    includes: list[str] = []
    capture = False
    for line in result.stderr.splitlines():
        stripped = line.strip()
        if stripped == "#include <...> search starts here:":
            capture = True
            continue
        if stripped == "End of search list.":
            break
        if capture and stripped:
            includes.append(stripped)

    if not includes:
        raise RuntimeError("Toolchain include directory query returned no paths")

    return includes


def find_headers(scan_roots: list[str], token: str) -> list[pathlib.Path]:
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
            if token in contents:
                headers.append(path)
    headers.sort()
    return headers


def find_annotated_record_names(header: pathlib.Path, token: str) -> list[str]:
    contents = header.read_text(encoding="utf-8")
    names = re.findall(
        rf"\b(?:class|struct)\s+{re.escape(token)}\s+([A-Za-z_]\w*)", contents
    )
    return sorted(dict.fromkeys(names))


def parse_json_stream(blob: str) -> list[dict]:
    decoder = json.JSONDecoder()
    index = 0
    items: list[dict] = []
    while index < len(blob):
        while index < len(blob) and blob[index].isspace():
            index += 1
        if index >= len(blob):
            break
        item, index = decoder.raw_decode(blob, index)
        items.append(item)
    return items


def ast_for_header(
    header: pathlib.Path, parse_args: list[str], dump_filter: str | None = None
) -> list[dict]:
    cmd = [*parse_args]
    if dump_filter is not None:
        cmd.extend(["-Xclang", f"-ast-dump-filter={dump_filter}"])
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
    return parse_json_stream(result.stdout)


def get_node_str(node: dict) -> str:
    if "range" not in node:
        _err = "No range in node"
        raise RuntimeError(_err)
    range_ = node["range"]
    if "begin" not in range_ or "end" not in range_:
        _err = "Range missing begin or end"
        raise RuntimeError(_err)
    if "spellingLoc" not in range_["begin"] or "spellingLoc" not in range_["end"]:
        _err = f"begin or end missing spellingLog: {range_['begin']}, {range_['end']}"
        raise RuntimeError(_err)
    begin = range_["begin"]["spellingLoc"]
    end = range_["end"]["spellingLoc"]
    if "line" not in begin or "col" not in begin:
        _err = f"Begin missing line or col: {begin}"
        raise RuntimeError(_err)
    if "line" not in end or "col" not in end:
        _err = f"End missing line or col: {end}"
        raise RuntimeError(_err)
    line_begin = begin["line"]
    line_end = end["line"]
    if "file" not in begin or "file" not in end:
        _err = f"Begin or end missing file: {begin}, {end}"
        raise RuntimeError(_err)
    if begin["file"] != end["file"]:
        _err = f"Begin and end file do not match: {begin['file']} vs {end['file']}"
        raise RuntimeError(_err)
    if "tokLen" not in end:
        _err = f"End missing tokLen: {end}"
        raise RuntimeError(_err)
    file = begin["file"]
    with open(file, "r", encoding="utf-8") as f:
        lines = f.readlines()[line_begin - 1 : line_end]

    if not lines:
        raise RuntimeError(f"Could not read source range for annotation: {node}")

    if line_begin == line_end:
        return lines[0][begin["col"] - 1 : end["col"] + end["tokLen"] - 1].strip()

    lines[0] = lines[0][begin["col"] - 1 :]
    lines[-1] = lines[-1][: end["col"] + end["tokLen"] - 1]
    return "".join(lines).strip()


def get_annotation_value(node: dict) -> str:
    value = get_node_str(node)
    if not value.startswith('clang::annotate("'):
        _err = f"Value does not start with clang annotation: {value}"
        raise RuntimeError(_err)
    return value.split('"')[1]


def has_annotation(node: dict, annotation: str) -> bool:
    for child in node.get("inner", []):
        if (
            child.get("kind") == "AnnotateAttr"
            and get_annotation_value(child) == annotation
        ):
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


def sanitize_filename(qualified_name: str) -> str:
    return qualified_name.strip(":").replace("::", "__") + ".hpp"


def collect_entries(
    node: dict,
    annotation: str,
    scope: list[str],
    callback: ExtractCallback,
    out: dict[str, CommonEntry],
) -> None:
    kind = node.get("kind")
    name = node.get("name")

    next_scope = scope
    if kind == "NamespaceDecl" and name:
        next_scope = [*scope, name]
    elif kind in RECORD_KINDS and name:
        next_scope = [*scope, name]
        if node.get("completeDefinition") and has_annotation(node, annotation):
            include_path = workspace_include_path(resolve_file(node) or "")
            if include_path is not None:
                qualified_name = "::" + "::".join(next_scope)
                out.setdefault(
                    qualified_name,
                    callback(
                        node,
                        CommonEntry(
                            qualified_name=qualified_name,
                            include_path=include_path,
                        ),
                    ),
                )

    for child in node.get("inner", []):
        collect_entries(child, annotation, next_scope, callback, out)


def render_all(entries: list[CommonEntry], group: str) -> str:
    lines = [
        "#pragma once",
        "",
        "// IWYU pragma: begin_exports",
        "",
        f'#include "Generated/{group}/Support.hpp"',
    ]
    for entry in entries:
        lines.append(
            f'#include "Generated/{group}/{sanitize_filename(entry.qualified_name)}"'
        )
    lines.append("")
    lines.append("// IWYU pragma: end_exports")
    return "\n".join(lines)


def write_file(path: pathlib.Path, contents: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents, encoding="ascii")


def clean_generated_headers(out_dir: pathlib.Path) -> None:
    for path in out_dir.glob("*.hpp"):
        path.unlink()


ExtractCallback = Callable[[dict, CommonEntry], EntryT]
RenderSupportCallback = Callable[[], str]
RenderCallback = Callable[[EntryT], str]


@dataclass(frozen=True)
class Dependencies:
    group: str
    token: str
    annotation: str
    render_support: RenderSupportCallback
    render: RenderCallback
    extract: ExtractCallback


def generate(deps: Dependencies) -> int:
    args = parse_args()
    parse_cmd: list[str]
    if args.envdata:
        parse_cmd = env_parse_args(load_envdata(pathlib.Path(args.envdata)))
    else:
        compdb = load_compdb(pathlib.Path(args.compdb))
        cpp_entries = workspace_cpp_entries(compdb)
        if not cpp_entries:
            raise RuntimeError("No workspace C++ compile commands found")
        parse_cmd = base_parse_args(cpp_entries[0])

    headers = find_headers(args.scan_root, deps.token)
    out_dir = pathlib.Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    entries: dict[str, CommonEntry] = {}
    for header in headers:
        names = find_annotated_record_names(header, deps.token)
        if not names:
            continue
        for ast in ast_for_header(header, parse_cmd):
            collect_entries(ast, deps.annotation, [], deps.extract, entries)

    entries_sorted = sorted(entries.values(), key=lambda s: s.qualified_name)
    clean_generated_headers(out_dir)
    write_file(out_dir / "Support.hpp", deps.render_support())
    write_file(out_dir / "All.hpp", render_all(entries_sorted, deps.group))
    for entry in entries_sorted:
        write_file(
            out_dir / sanitize_filename(entry.qualified_name),
            deps.render(entry),
        )

    return 0
