#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Types.hpp" // IWYU pragma: keep
#include "magic_enum/magic_enum.hpp"
#include <cstdint>
#include <string_view>

namespace Totem::CommandBackend::detail {

using Token = std::string_view;

template <typename T> inline constexpr int arg_type_tag_storage = 0;

template <typename T> constexpr const void *arg_type_tag() {
    return &arg_type_tag_storage<T>;
}

inline bool parse_u32(std::string_view str, uint32_t &out) {
    if (str.empty()) {
        return false;
    }

    uint32_t value = 0;
    for (char chr : str) {
        if (chr < '0' || chr > '9') {
            return false;
        }
        auto digit = static_cast<uint32_t>(chr - '0');
        uint32_t next = (value * 10U) + digit;
        if (next < value) {
            return false;
        }
        value = next;
    }

    out = value;
    return true;
}

inline bool parse_i32(std::string_view str, int32_t &out) {
    if (str.empty()) {
        return false;
    }

    bool neg = false;
    if (str.front() == '-') {
        neg = true;
        str.remove_prefix(1);
        if (str.empty()) {
            return false;
        }
    }

    uint32_t tmp = 0;
    if (!parse_u32(str, tmp)) {
        return false;
    }

    if (neg) {
        if (tmp > 2147483648U) {
            return false;
        }
        out = (tmp == 2147483648U) ? static_cast<int32_t>(-2147483647 - 1)
                                   : -static_cast<int32_t>(tmp);
    } else {
        if (tmp > 2147483647U) {
            return false;
        }
        out = static_cast<int32_t>(tmp);
    }

    return true;
}

inline bool parse_sv(std::string_view str, std::string_view &out) {
    out = str;
    return true;
}

inline bool parse_bool(std::string_view str, bool &out) {
    if (str == "true" || str == "1" || str == "on" || str == "yes") {
        out = true;
        return true;
    }
    if (str == "false" || str == "0" || str == "off" || str == "no") {
        out = false;
        return true;
    }
    return false;
}

template <typename Enum>
inline bool parse_enum(std::string_view str, Enum &out) {
    auto opt = magic_enum::enum_cast<Enum>(str, magic_enum::case_insensitive);
    if (!opt) {
        return false;
    }
    out = *opt;
    return true;
}

template <typename T>
CommandDesc::ParseResult parse_adapter(std::string_view input, void *out);

template <>
inline CommandDesc::ParseResult parse_adapter<uint32_t>(std::string_view input,
                                                        void *out) {
    auto &value = *static_cast<uint32_t *>(out);
    return {parse_u32(input, value)};
}

template <>
inline CommandDesc::ParseResult parse_adapter<int32_t>(std::string_view input,
                                                       void *out) {
    auto &value = *static_cast<int32_t *>(out);
    return {parse_i32(input, value)};
}

template <>
inline CommandDesc::ParseResult
parse_adapter<std::string_view>(std::string_view input, void *out) {
    auto &value = *static_cast<std::string_view *>(out);
    return {parse_sv(input, value)};
}

template <>
inline CommandDesc::ParseResult parse_adapter<bool>(std::string_view input,
                                                    void *out) {
    auto &value = *static_cast<bool *>(out);
    return {parse_bool(input, value)};
}

template <typename Enum>
inline CommandDesc::ParseResult parse_adapter(std::string_view input,
                                              void *out) {
    auto &value = *static_cast<Enum *>(out);
    return {parse_enum<Enum>(input, value)};
}

template <typename T>
constexpr CommandDesc::Argument arg(std::string_view name,
                                    CommandDesc::ArgRequirement requirement =
                                        CommandDesc::ArgRequirement::Required) {
    return CommandDesc::Argument{.name = name,
                                 .requirement = requirement,
                                 .type = arg_type_tag<T>(),
                                 .parse = &parse_adapter<T>};
}

} // namespace Totem::CommandBackend::detail
