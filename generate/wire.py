#!/usr/bin/env python3

from __future__ import annotations

from dataclasses import dataclass
import argparse
import pathlib
import re
import sys
from typing import Any

from lib.fun import (
    CommonEntry,
    Dependencies,
    generate,
    write_file,
)


GROUP = "Wire"
TOKEN = "WIRE_MSG"
ANNOTATION = "wire"


@dataclass(frozen=True)
class FieldEntry:
    name: str
    qual_type: str
    desugared_type: str
    default: Any | None


@dataclass(frozen=True)
class WireEntry(CommonEntry):
    fields: tuple[FieldEntry, ...]


@dataclass(frozen=True)
class EnumValueEntry:
    name: str
    value: int


@dataclass(frozen=True)
class EnumEntry:
    qualified_name: str
    underlying_type: str
    values: tuple[EnumValueEntry, ...]


@dataclass(frozen=True)
class WrapperEntry:
    qualified_name: str
    value_type: str


ENUMS: dict[str, EnumEntry] = {}
WRAPPERS: dict[str, WrapperEntry] = {}
REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WELL_KNOWN_ENUMS = {
    "::Totem::Data::PubSub::NodeId",
    "::Totem::Data::PubSub::Topic",
}

STD_ARRAY_RE = re.compile(r"^std::array<(?P<item>.+), (?P<count>[0-9]+)>$")
C_ARRAY_RE = re.compile(r"^(?P<item>.+)\[(?P<count>[0-9]+)\]$")
RECORD_KINDS = {"CXXRecordDecl", "ClassTemplateSpecializationDecl"}

SCALAR_TYPES: dict[str, tuple[int, bool]] = {
    "bool": (1, False),
    "char": (1, True),
    "signed char": (1, True),
    "unsigned char": (1, False),
    "std::byte": (1, False),
    "int8_t": (1, True),
    "uint8_t": (1, False),
    "short": (2, True),
    "short int": (2, True),
    "signed short": (2, True),
    "signed short int": (2, True),
    "unsigned short": (2, False),
    "unsigned short int": (2, False),
    "int16_t": (2, True),
    "uint16_t": (2, False),
    "int": (4, True),
    "signed int": (4, True),
    "unsigned int": (4, False),
    "int32_t": (4, True),
    "uint32_t": (4, False),
    "long": (4, True),
    "long int": (4, True),
    "unsigned long": (4, False),
    "unsigned long int": (4, False),
    "long long": (8, True),
    "long long int": (8, True),
    "unsigned long long": (8, False),
    "unsigned long long int": (8, False),
    "int64_t": (8, True),
    "uint64_t": (8, False),
    "size_t": (8, False),
}

TYPE_ALIASES = {
    "std::uint8_t": "uint8_t",
    "std::uint16_t": "uint16_t",
    "std::uint32_t": "uint32_t",
    "std::uint64_t": "uint64_t",
    "std::int8_t": "int8_t",
    "std::int16_t": "int16_t",
    "std::int32_t": "int32_t",
    "std::int64_t": "int64_t",
}


def _strip_outer_type_noise(type_name: str) -> str:
    cleaned = " ".join(type_name.replace("const ", "").split())
    for prefix in ("struct ", "class ", "enum "):
        if cleaned.startswith(prefix):
            cleaned = cleaned[len(prefix) :]
    if cleaned.endswith(" const"):
        cleaned = cleaned[: -len(" const")]
    return TYPE_ALIASES.get(cleaned, cleaned)


def canonical_type(type_name: str) -> str:
    cleaned = _strip_outer_type_noise(type_name)
    match = STD_ARRAY_RE.match(cleaned)
    if match is not None:
        item = canonical_type(match.group("item"))
        return f"std::array<{item}, {match.group('count')}>"
    if "::" in cleaned and not cleaned.startswith("::") and not cleaned.startswith("std::"):
        return f"::{cleaned}"
    return cleaned


def _type_blob(field_node: dict) -> tuple[str, str]:
    type_node = field_node.get("type", {})
    qual_type = type_node.get("qualType", "")
    desugared_type = type_node.get("desugaredQualType", qual_type)
    return canonical_type(qual_type), canonical_type(desugared_type)


def _parse_int(value: Any) -> int | None:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if not isinstance(value, str):
        return None
    try:
        return int(value, 0)
    except ValueError:
        return None


def _constant_expr_value(node: dict) -> int | None:
    value = _parse_int(node.get("value"))
    if value is not None:
        return value
    for child in node.get("inner", []):
        value = _constant_expr_value(child)
        if value is not None:
            return value
    return None


def _literal_default(node: dict) -> Any | None:
    kind = node.get("kind")

    if kind == "CXXBoolLiteralExpr":
        value = node.get("value")
        if isinstance(value, bool):
            return value
        if isinstance(value, str):
            return value.lower() == "true"

    if kind in {"IntegerLiteral", "ConstantExpr", "CharacterLiteral"}:
        value = _parse_int(node.get("value"))
        if value is not None:
            return value

    if kind == "DeclRefExpr":
        referenced = node.get("referencedDecl", {})
        if referenced.get("kind") == "EnumConstantDecl":
            return referenced.get("name")

    for child in node.get("inner", []):
        value = _literal_default(child)
        if value is not None:
            return value
    return None


def _field_default(field_node: dict, type_name: str) -> Any | None:
    if not field_node.get("hasInClassInitializer"):
        return None
    if type_name != "bool" and type_name not in SCALAR_TYPES and type_name not in ENUMS:
        return None
    return _literal_default(field_node)


def extract(node: dict, entry: CommonEntry) -> WireEntry:
    fields = []
    for child in node.get("inner", []):
        if child.get("kind") != "FieldDecl" or not child.get("name"):
            continue
        qual_type, desugared_type = _type_blob(child)
        fields.append(
            FieldEntry(
                name=child["name"],
                qual_type=qual_type,
                desugared_type=desugared_type,
                default=_field_default(child, desugared_type or qual_type),
            )
        )
    return WireEntry(
        qualified_name=entry.qualified_name,
        include_path=entry.include_path,
        fields=tuple(fields),
    )


def _enum_underlying_type(node: dict) -> str:
    fixed = node.get("fixedUnderlyingType", {})
    qual_type = fixed.get("qualType")
    if isinstance(qual_type, str) and qual_type:
        return canonical_type(qual_type)
    return "int"


def _enum_qualified_name(node: dict, scope: list[str]) -> str | None:
    for child in node.get("inner", []):
        if child.get("kind") != "EnumConstantDecl":
            continue
        qual_type = child.get("type", {}).get("qualType")
        if isinstance(qual_type, str) and qual_type:
            return canonical_type(qual_type)
    name = node.get("name")
    if not name:
        return None
    if scope:
        return "::" + "::".join([*scope, name])
    return name


def _record_qualified_name(name: str, scope: list[str]) -> str:
    if scope:
        return "::" + "::".join([*scope, name])
    return name


def _collect_wrapper(node: dict, scope: list[str]) -> None:
    if not node.get("completeDefinition") or not node.get("name"):
        return
    fields = [
        child
        for child in node.get("inner", [])
        if child.get("kind") == "FieldDecl" and child.get("name")
    ]
    if len(fields) != 1 or fields[0].get("name") != "value":
        return
    _, desugared_type = _type_blob(fields[0])
    qualified_name = _record_qualified_name(node["name"], scope)
    WRAPPERS.setdefault(
        qualified_name,
        WrapperEntry(qualified_name=qualified_name, value_type=desugared_type),
    )


def _collect_metadata(node: dict, scope: list[str]) -> None:
    kind = node.get("kind")
    name = node.get("name")

    next_scope = scope
    if kind == "NamespaceDecl" and name:
        next_scope = [*scope, name]
    elif kind in RECORD_KINDS and name:
        _collect_wrapper(node, scope)
        next_scope = [*scope, name]
    elif kind == "EnumDecl" and name:
        qualified_name = _enum_qualified_name(node, scope)
        if qualified_name is not None:
            values: list[EnumValueEntry] = []
            last_value = -1
            for child in node.get("inner", []):
                if child.get("kind") != "EnumConstantDecl" or not child.get("name"):
                    continue
                value = _constant_expr_value(child)
                if value is None:
                    value = last_value + 1
                values.append(EnumValueEntry(name=child["name"], value=value))
                last_value = value
            if values:
                ENUMS.setdefault(
                    qualified_name,
                    EnumEntry(
                        qualified_name=qualified_name,
                        underlying_type=_enum_underlying_type(node),
                        values=tuple(values),
                    ),
                )

    for child in node.get("inner", []):
        _collect_metadata(child, next_scope)


def observe_ast(asts: list[dict], _header: pathlib.Path) -> None:
    for ast in asts:
        _collect_metadata(ast, [])


def _eval_enum_expr(expr: str) -> int:
    text = expr.strip()
    text = re.sub(r"([0-9A-Fa-fx]+)[UuLl]+", r"\1", text)
    if "<<" in text:
        left, right = text.split("<<", 1)
        return int(left.strip(), 0) << int(right.strip(), 0)
    return int(text, 0)


def _collect_pubsub_enums() -> None:
    header = REPO_ROOT / "include" / "Data" / "PubSub.hpp"
    if not header.exists():
        return

    contents = re.sub(r"//.*", "", header.read_text(encoding="utf-8"))
    pattern = re.compile(
        r"enum\s+class\s+(?P<name>NodeId|Topic)\s*:\s*"
        r"(?P<underlying>[A-Za-z0-9_:]+)\s*\{(?P<body>.*?)\};",
        re.S,
    )
    for match in pattern.finditer(contents):
        enum_name = match.group("name")
        qualified_name = f"::Totem::Data::PubSub::{enum_name}"
        values: list[EnumValueEntry] = []
        last_value = -1
        for raw_item in match.group("body").split(","):
            item = raw_item.strip()
            if not item:
                continue
            if "=" in item:
                name, expr = item.split("=", 1)
                value = _eval_enum_expr(expr)
            else:
                name = item
                value = last_value + 1
            values.append(EnumValueEntry(name=name.strip(), value=value))
            last_value = value
        if values:
            ENUMS[qualified_name] = EnumEntry(
                qualified_name=qualified_name,
                underlying_type=canonical_type(match.group("underlying")),
                values=tuple(values),
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

    if entry.fields:
        lines.extend(
            [
                "    static constexpr auto fields = std::make_tuple(",
                ",\n".join(
                    f'        Field<&Type::{field.name}>{{"{field.name}"}}'
                    for field in entry.fields
                ),
                "    );",
            ]
        )
    else:
        lines.append("    static constexpr auto fields = std::make_tuple();")

    lines.extend(["};", "", "} // namespace Totem::Generated::Wire", ""])
    return "\n".join(lines)


def _array_parts(type_name: str) -> tuple[str, int] | None:
    match = STD_ARRAY_RE.match(type_name)
    if match is not None:
        return canonical_type(match.group("item")), int(match.group("count"))
    match = C_ARRAY_RE.match(type_name)
    if match is not None:
        return canonical_type(match.group("item")), int(match.group("count"))
    return None


def _field_kind(type_name: str, model_names: set[str]) -> dict[str, Any]:
    if type_name == "bool":
        return {
            "kind": "bool",
            "width": 1,
            "signed": False,
            "enum_name": None,
            "model_name": None,
            "array_len": None,
            "element_type": None,
            "element_kind": None,
            "element_width": None,
            "element_signed": None,
            "element_enum_name": None,
            "element_model_name": None,
        }
    if type_name in SCALAR_TYPES:
        width, signed = SCALAR_TYPES[type_name]
        return {
            "kind": "scalar",
            "width": width,
            "signed": signed,
            "enum_name": None,
            "model_name": None,
            "array_len": None,
            "element_type": None,
            "element_kind": None,
            "element_width": None,
            "element_signed": None,
            "element_enum_name": None,
            "element_model_name": None,
        }
    if type_name in ENUMS:
        enum = ENUMS[type_name]
        width, signed = SCALAR_TYPES.get(enum.underlying_type, (4, True))
        return {
            "kind": "enum",
            "width": width,
            "signed": signed,
            "enum_name": type_name,
            "model_name": None,
            "array_len": None,
            "element_type": None,
            "element_kind": None,
            "element_width": None,
            "element_signed": None,
            "element_enum_name": None,
            "element_model_name": None,
        }
    if type_name in model_names:
        return {
            "kind": "model",
            "width": None,
            "signed": None,
            "enum_name": None,
            "model_name": type_name,
            "array_len": None,
            "element_type": None,
            "element_kind": None,
            "element_width": None,
            "element_signed": None,
            "element_enum_name": None,
            "element_model_name": None,
        }

    if type_name in WRAPPERS:
        return _field_kind(WRAPPERS[type_name].value_type, model_names)

    array = _array_parts(type_name)
    if array is not None:
        element_type, array_len = array
        element = _field_kind(element_type, model_names)
        return {
            "kind": "array",
            "width": None,
            "signed": None,
            "enum_name": None,
            "model_name": None,
            "array_len": array_len,
            "element_type": element_type,
            "element_kind": element["kind"],
            "element_width": element["width"],
            "element_signed": element["signed"],
            "element_enum_name": element["enum_name"],
            "element_model_name": element["model_name"],
        }

    return {
        "kind": "unsupported",
        "width": None,
        "signed": None,
        "enum_name": None,
        "model_name": None,
        "array_len": None,
        "element_type": None,
        "element_kind": None,
        "element_width": None,
        "element_signed": None,
        "element_enum_name": None,
        "element_model_name": None,
    }


def _render_python_prelude() -> str:
    return '''"""Generated wire models. Do not edit."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class EnumValue:
    name: str
    value: int


@dataclass(frozen=True)
class EnumDesc:
    qualified_name: str
    underlying_type: str
    values: tuple[EnumValue, ...]

    def value_by_name(self, name: str) -> int:
        for value in self.values:
            if value.name == name:
                return value.value
        raise KeyError(name)

    def name_by_value(self, value: int) -> str:
        for item in self.values:
            if item.value == value:
                return item.name
        raise KeyError(value)


@dataclass(frozen=True)
class FieldDesc:
    name: str
    type_name: str
    kind: str
    default: Any | None
    width: int | None = None
    signed: bool | None = None
    enum_name: str | None = None
    model_name: str | None = None
    array_len: int | None = None
    element_type: str | None = None
    element_kind: str | None = None
    element_width: int | None = None
    element_signed: bool | None = None
    element_enum_name: str | None = None
    element_model_name: str | None = None


@dataclass(frozen=True)
class ModelDesc:
    qualified_name: str
    include_path: str
    fields: tuple[FieldDesc, ...]


class UnsupportedFieldError(ValueError):
    pass


def _coerce_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, int):
        return value != 0
    if isinstance(value, str):
        lowered = value.strip().lower()
        if lowered in {"1", "true", "yes", "on"}:
            return True
        if lowered in {"0", "false", "no", "off"}:
            return False
    raise ValueError(f"cannot parse bool value {value!r}")


def _coerce_int(value: Any) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value.strip(), 0)
    raise ValueError(f"cannot parse integer value {value!r}")


def _encode_int(value: Any, width: int, signed: bool) -> bytes:
    number = _coerce_int(value)
    bits = width * 8
    if signed:
        low = -(1 << (bits - 1))
        high = (1 << (bits - 1)) - 1
    else:
        low = 0
        high = (1 << bits) - 1
    if number < low or number > high:
        raise OverflowError(f"{number} does not fit in {bits}-bit {'signed' if signed else 'unsigned'} storage")
    return number.to_bytes(width, "little", signed=signed)


def _enum_value(enum_name: str, value: Any) -> int:
    if isinstance(value, str):
        item = value.strip()
        if "::" in item:
            item = item.rsplit("::", 1)[1]
        try:
            return ENUMS[enum_name].value_by_name(item)
        except KeyError:
            pass
        return int(item, 0)
    return _coerce_int(value)


def default_value(field: FieldDesc) -> Any:
    if field.default is not None:
        return field.default
    if field.kind == "bool":
        return False
    if field.kind == "scalar":
        return 0
    if field.kind == "enum":
        enum = ENUMS[field.enum_name or ""]
        return enum.values[0].name if enum.values else None
    if field.kind == "array":
        return [default_array_element(field) for _ in range(field.array_len or 0)]
    if field.kind == "model":
        return default_model(field.model_name or "")
    return None


def default_array_element(field: FieldDesc) -> Any:
    if field.element_kind == "bool":
        return False
    if field.element_kind == "scalar":
        return 0
    if field.element_kind == "enum":
        enum = ENUMS[field.element_enum_name or ""]
        return enum.values[0].name if enum.values else None
    if field.element_kind == "model":
        return default_model(field.element_model_name or "")
    return None


def default_model(model_name: str) -> dict[str, Any]:
    model = MODELS[model_name]
    return {field.name: default_value(field) for field in model.fields}


def encode_model(model_name: str, values: dict[str, Any] | None = None) -> bytes:
    model = MODELS[model_name]
    merged = default_model(model_name)
    if values:
        merged.update(values)
    return b"".join(encode_field(field, merged.get(field.name)) for field in model.fields)


def encode_field(field: FieldDesc, value: Any) -> bytes:
    if field.kind == "bool":
        return bytes([1 if _coerce_bool(value) else 0])
    if field.kind == "scalar":
        return _encode_int(value, field.width or 0, bool(field.signed))
    if field.kind == "enum":
        return _encode_int(
            _enum_value(field.enum_name or "", value),
            field.width or 0,
            bool(field.signed),
        )
    if field.kind == "array":
        return _encode_array(field, value)
    if field.kind == "model":
        if not isinstance(value, dict):
            raise ValueError(f"model field {field.name} needs a dict value")
        return encode_model(field.model_name or "", value)
    raise UnsupportedFieldError(
        f"field {field.name} has unsupported wire type {field.type_name}"
    )


def _encode_array(field: FieldDesc, value: Any) -> bytes:
    if isinstance(value, str) and field.element_kind == "scalar":
        cleaned = "".join(value.split())
        values = list(bytes.fromhex(cleaned))
    elif isinstance(value, (bytes, bytearray)) and field.element_kind == "scalar":
        values = list(value)
    else:
        values = list(value or [])
    expected = field.array_len or 0
    if len(values) != expected:
        raise ValueError(f"array field {field.name} needs {expected} values, got {len(values)}")
    parts = []
    for item in values:
        if field.element_kind == "bool":
            parts.append(bytes([1 if _coerce_bool(item) else 0]))
        elif field.element_kind == "scalar":
            parts.append(_encode_int(item, field.element_width or 0, bool(field.element_signed)))
        elif field.element_kind == "enum":
            parts.append(
                _encode_int(
                    _enum_value(field.element_enum_name or "", item),
                    field.element_width or 0,
                    bool(field.element_signed),
                )
            )
        elif field.element_kind == "model":
            if not isinstance(item, dict):
                raise ValueError(f"array field {field.name} needs dict elements")
            parts.append(encode_model(field.element_model_name or "", item))
        else:
            raise UnsupportedFieldError(
                f"array field {field.name} has unsupported element type {field.element_type}"
            )
    return b"".join(parts)


'''


def _render_enum(enum: EnumEntry) -> str:
    values = ", ".join(
        f"EnumValue({value.name!r}, {value.value!r})" for value in enum.values
    )
    if values:
        values = f"({values},)"
    else:
        values = "()"
    return (
        f"    {enum.qualified_name!r}: EnumDesc("
        f"{enum.qualified_name!r}, {enum.underlying_type!r}, {values}),"
    )


def _render_field(field: FieldEntry, model_names: set[str]) -> str:
    info = _field_kind(field.desugared_type or field.qual_type, model_names)
    kwargs = [
        f"name={field.name!r}",
        f"type_name={field.desugared_type or field.qual_type!r}",
        f"kind={info['kind']!r}",
        f"default={field.default!r}",
    ]
    for key in (
        "width",
        "signed",
        "enum_name",
        "model_name",
        "array_len",
        "element_type",
        "element_kind",
        "element_width",
        "element_signed",
        "element_enum_name",
        "element_model_name",
    ):
        if info[key] is not None:
            kwargs.append(f"{key}={info[key]!r}")
    return "FieldDesc(" + ", ".join(kwargs) + ")"


def _render_model(entry: WireEntry, model_names: set[str]) -> str:
    fields = ", ".join(_render_field(field, model_names) for field in entry.fields)
    if fields:
        fields = f"({fields},)"
    else:
        fields = "()"
    return (
        f"    {entry.qualified_name!r}: ModelDesc("
        f"{entry.qualified_name!r}, {entry.include_path!r}, {fields}),"
    )


def render_python(entries: list[WireEntry], args: argparse.Namespace) -> None:
    if not args.py_out:
        return

    _collect_pubsub_enums()
    model_names = {entry.qualified_name for entry in entries}
    referenced_enums = set(WELL_KNOWN_ENUMS).intersection(ENUMS)
    for entry in entries:
        for field in entry.fields:
            type_name = field.desugared_type or field.qual_type
            if type_name in ENUMS:
                referenced_enums.add(type_name)
            array = _array_parts(type_name)
            if array is not None and array[0] in ENUMS:
                referenced_enums.add(array[0])
    enum_lines = [_render_enum(ENUMS[name]) for name in sorted(referenced_enums)]
    model_lines = [_render_model(entry, model_names) for entry in entries]

    lines = [_render_python_prelude()]
    lines.append("ENUMS = {")
    lines.extend(enum_lines)
    lines.append("}")
    lines.append("MODELS = {")
    lines.extend(model_lines)
    lines.append("}")
    lines.append("")

    write_file(pathlib.Path(args.py_out), "\n".join(lines))


deps = Dependencies(
    group=GROUP,
    token=TOKEN,
    annotation=ANNOTATION,
    render_support=render_support,
    render=render,
    extract=extract,
    observe_ast=observe_ast,
    render_extra=render_python,
)

if __name__ == "__main__":
    sys.exit(generate(deps))
