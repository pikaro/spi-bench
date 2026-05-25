#pragma once

#include "Generated/Wire/All.hpp" // IWYU pragma: keep

#include "Generated/Wire/Support.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Totem::PubSubBackend::detail {

template <typename T>
using decay_field_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T, typename = void>
struct IsGeneratedWireMessage : std::false_type {};

template <typename T>
struct IsGeneratedWireMessage<
    T, std::void_t<decltype(Generated::Wire::FieldList<T>::fields)>>
    : std::true_type {};

template <typename T>
inline constexpr bool is_wire_message_v = IsGeneratedWireMessage<T>::value;

template <typename T>
using value_member_ref_t = decltype(std::declval<decay_field_t<T> &>().value);

template <typename T>
using value_member_t = decay_field_t<value_member_ref_t<T>>;

template <typename T, typename = void>
struct IsTransparentValueWrapper : std::false_type {};

template <typename T>
struct IsTransparentValueWrapper<T, std::void_t<value_member_ref_t<T>>>
    : std::bool_constant<
          std::is_class_v<decay_field_t<T>> &&
          std::is_aggregate_v<decay_field_t<T>> &&
          std::is_trivially_copyable_v<decay_field_t<T>> &&
          !std::is_const_v<std::remove_reference_t<value_member_ref_t<T>>> &&
          sizeof(decay_field_t<T>) == sizeof(value_member_t<T>)> {};

template <typename T>
inline constexpr bool is_transparent_value_wrapper_v =
    IsTransparentValueWrapper<T>::value;

template <typename T>
inline constexpr bool is_wire_codec_type_v =
    is_wire_message_v<T> || is_transparent_value_wrapper_v<T>;

namespace wire {

constexpr ReturnCode ok_code() { return ReturnCode::from(CoreError::Ok); }

constexpr ReturnCode error_code(CoreError err) { return ReturnCode::from(err); }

template <typename T>
std::expected<T, ReturnCode> unexpected_code(ReturnCode ret) {
    return std::expected<T, ReturnCode>{std::unexpect, ret};
}

template <typename T> struct always_false : std::false_type {};

template <auto MemberPtr> struct MemberPointerTraits;

template <typename ClassT, typename FieldT, FieldT ClassT::*MemberPtr>
struct MemberPointerTraits<MemberPtr> {
    using Class = ClassT;
    using Field = FieldT;
};

template <typename T> struct is_std_array : std::false_type {};

template <typename T, size_t N>
struct is_std_array<std::array<T, N>> : std::true_type {};

template <typename T>
inline constexpr bool is_std_array_v = is_std_array<decay_field_t<T>>::value;

template <typename T>
inline constexpr bool is_c_array_v = std::is_array_v<decay_field_t<T>>;

template <typename T> struct scalar_storage_type {
    using type = decay_field_t<T>;
};

template <> struct scalar_storage_type<bool> {
    using type = uint8_t;
};

template <typename T> struct scalar_storage_type_enum {
    using type = std::underlying_type_t<decay_field_t<T>>;
};

template <typename T, bool IsEnum> struct scalar_storage_selector;

template <typename T> struct scalar_storage_selector<T, false> {
    using type = typename scalar_storage_type<decay_field_t<T>>::type;
};

template <typename T> struct scalar_storage_selector<T, true> {
    using type = typename scalar_storage_type_enum<T>::type;
};

template <typename T>
using scalar_storage_t =
    typename scalar_storage_selector<T, std::is_enum_v<decay_field_t<T>>>::type;

struct Reader {
    ReturnCode (*read)(void *owner, size_t offset, std::span<std::byte> out);
    void *owner = nullptr;
    size_t size = 0;

    [[nodiscard]] bool valid() const { return read != nullptr; }
};

struct SpanReaderState {
    std::span<const std::byte> span;
};

inline ReturnCode readSpan(void *owner, size_t offset,
                           std::span<std::byte> out) {
    auto *state = static_cast<SpanReaderState *>(owner);
    if (offset + out.size() > state->span.size()) {
        return error_code(CoreError::Underflow);
    }
    std::memcpy(out.data(), state->span.data() + offset, out.size());
    return ok_code();
}

struct SpanReader {
    Reader reader;
    SpanReaderState state;

    explicit SpanReader(std::span<const std::byte> span)
        : reader{.read = readSpan, .owner = &state, .size = span.size()},
          state{.span = span} {}
};

struct DecodeCursor {
    Reader reader{};
    size_t offset = 0;
};

template <typename T> constexpr size_t encoded_size();

template <typename T>
constexpr ReturnCode encode_value(const T &value, std::span<std::byte> out,
                                  size_t &offset);

template <typename T>
std::expected<T, ReturnCode> decode_value(DecodeCursor &cursor);

template <typename T> constexpr auto as_unsigned_bits(T value) {
    using CleanT = decay_field_t<T>;
    using UnsignedT = std::make_unsigned_t<CleanT>;
    return static_cast<UnsignedT>(value);
}

template <typename T>
constexpr ReturnCode encode_scalar(T value, std::span<std::byte> out,
                                   size_t &offset) {
    using CleanT = decay_field_t<T>;
    static_assert(std::is_integral_v<CleanT> || std::is_enum_v<CleanT>);

    using StorageT = scalar_storage_t<CleanT>;
    using UnsignedT = std::make_unsigned_t<StorageT>;

    constexpr size_t width = sizeof(StorageT);
    if (offset + width > out.size()) {
        return error_code(CoreError::Overflow);
    }

    UnsignedT bits{};
    if constexpr (std::is_same_v<CleanT, bool>) {
        bits = value ? 1U : 0U;
    } else if constexpr (std::is_enum_v<CleanT>) {
        bits = static_cast<UnsignedT>(static_cast<StorageT>(value));
    } else {
        bits = as_unsigned_bits(static_cast<StorageT>(value));
    }

    for (size_t i = 0; i < width; ++i) {
        out[offset + i] = static_cast<std::byte>((bits >> (i * 8U)) & 0xFFU);
    }
    offset += width;
    return ok_code();
}

template <typename T>
std::expected<T, ReturnCode> decode_scalar(DecodeCursor &cursor) {
    using CleanT = decay_field_t<T>;
    static_assert(std::is_integral_v<CleanT> || std::is_enum_v<CleanT>);

    using StorageT = scalar_storage_t<CleanT>;
    using UnsignedT = std::make_unsigned_t<StorageT>;

    constexpr size_t width = sizeof(StorageT);
    if (!cursor.reader.valid()) {
        return unexpected_code<T>(error_code(CoreError::InvalidArgument));
    }
    if (cursor.offset + width > cursor.reader.size) {
        return unexpected_code<T>(error_code(CoreError::Underflow));
    }

    std::array<std::byte, width> raw{};
    auto ret = cursor.reader.read(cursor.reader.owner, cursor.offset, raw);
    if (!ret.ok()) {
        return unexpected_code<T>(ret);
    }

    UnsignedT bits{};
    for (size_t i = 0; i < width; ++i) {
        bits |= static_cast<UnsignedT>(std::to_integer<uint8_t>(raw[i]))
                << (i * 8U);
    }
    cursor.offset += width;

    if constexpr (std::is_same_v<CleanT, bool>) {
        return static_cast<bool>(bits != 0U);
    }
    return static_cast<CleanT>(static_cast<StorageT>(bits));
}

template <typename TupleT, size_t... I>
constexpr size_t
encoded_size_from_fields(std::index_sequence<I...> /*unused*/) {
    return (
        encoded_size<typename MemberPointerTraits<decay_field_t<
            decltype(std::get<I>(std::declval<TupleT>()))>::member>::Field>() +
        ... + 0U);
}

template <typename T, typename TupleT, size_t... I>
ReturnCode encode_fields(const T &value, const TupleT &fields,
                         std::span<std::byte> out, size_t &offset,
                         std::index_sequence<I...> /*unused*/) {
    auto ret = ok_code();
    (
        [&] {
            if (!ret.ok()) {
                return;
            }
            const auto &field = std::get<I>(fields);
            using FieldMeta = decay_field_t<decltype(field)>;
            ret.combine(encode_value(value.*FieldMeta::member, out, offset));
        }(),
        ...);
    return ret;
}

template <typename T, typename TupleT, size_t... I>
std::expected<T, ReturnCode>
decode_fields(const TupleT &fields, DecodeCursor &cursor,
              std::index_sequence<I...> /*unused*/) {
    T value{};
    auto ret = ok_code();
    (
        [&] {
            if (!ret.ok()) {
                return;
            }
            const auto &field = std::get<I>(fields);
            using FieldMeta = decay_field_t<decltype(field)>;
            using FieldT =
                typename MemberPointerTraits<FieldMeta::member>::Field;
            auto decoded = decode_value<FieldT>(cursor);
            if (!decoded) {
                ret = decoded.error();
                return;
            }
            value.*FieldMeta::member = *decoded;
        }(),
        ...);

    if (!ret.ok()) {
        return unexpected_code<T>(ret);
    }
    return value;
}

template <typename T> constexpr size_t encoded_size() {
    using CleanT = decay_field_t<T>;

    if constexpr (std::is_same_v<CleanT, bool>) {
        return sizeof(uint8_t);
    } else if constexpr (std::is_integral_v<CleanT>) {
        return sizeof(CleanT);
    } else if constexpr (std::is_enum_v<CleanT>) {
        return sizeof(std::underlying_type_t<CleanT>);
    } else if constexpr (is_std_array_v<CleanT>) {
        using ElementT = typename CleanT::value_type;
        return std::tuple_size_v<CleanT> * encoded_size<ElementT>();
    } else if constexpr (is_c_array_v<CleanT>) {
        using ElementT = std::remove_extent_t<CleanT>;
        return std::extent_v<CleanT> * encoded_size<ElementT>();
    } else if constexpr (is_wire_message_v<CleanT>) {
        using FieldsT =
            decay_field_t<decltype(Generated::Wire::FieldList<CleanT>::fields)>;
        return encoded_size_from_fields<FieldsT>(
            std::make_index_sequence<std::tuple_size_v<FieldsT>>{});
    } else if constexpr (is_transparent_value_wrapper_v<CleanT>) {
        return encoded_size<value_member_t<CleanT>>();
    } else {
        static_assert(always_false<CleanT>::value,
                      "Unsupported wire field type");
        return 0;
    }
}

template <typename T>
constexpr ReturnCode encode_value(const T &value, std::span<std::byte> out,
                                  size_t &offset) {
    using CleanT = decay_field_t<T>;

    if constexpr (std::is_same_v<CleanT, bool> || std::is_integral_v<CleanT> ||
                  std::is_enum_v<CleanT>) {
        return encode_scalar(value, out, offset);
    } else if constexpr (is_std_array_v<CleanT>) {
        for (const auto &item : value) {
            auto ret = encode_value(item, out, offset);
            if (!ret.ok()) {
                return ret;
            }
        }
        return ok_code();
    } else if constexpr (is_c_array_v<CleanT>) {
        for (const auto &item : value) {
            auto ret = encode_value(item, out, offset);
            if (!ret.ok()) {
                return ret;
            }
        }
        return ok_code();
    } else if constexpr (is_wire_message_v<CleanT>) {
        constexpr auto &fields = Generated::Wire::FieldList<CleanT>::fields;
        using FieldsT = decay_field_t<decltype(fields)>;
        return encode_fields(
            value, fields, out, offset,
            std::make_index_sequence<std::tuple_size_v<FieldsT>>{});
    } else if constexpr (is_transparent_value_wrapper_v<CleanT>) {
        return encode_value(value.value, out, offset);
    } else {
        static_assert(always_false<CleanT>::value,
                      "Unsupported wire field type");
        return error_code(CoreError::InvalidArgument);
    }
}

template <typename T>
std::expected<T, ReturnCode> decode_value(DecodeCursor &cursor) {
    using CleanT = decay_field_t<T>;

    if constexpr (std::is_same_v<CleanT, bool> || std::is_integral_v<CleanT> ||
                  std::is_enum_v<CleanT>) {
        return decode_scalar<CleanT>(cursor);
    } else if constexpr (is_std_array_v<CleanT>) {
        CleanT value{};
        for (auto &item : value) {
            auto decoded = decode_value<typename CleanT::value_type>(cursor);
            if (!decoded) {
                return std::unexpected(decoded.error());
            }
            item = *decoded;
        }
        return value;
    } else if constexpr (is_c_array_v<CleanT>) {
        CleanT value{};
        for (auto &item : value) {
            using ElementT = std::remove_extent_t<CleanT>;
            auto decoded = decode_value<ElementT>(cursor);
            if (!decoded) {
                return std::unexpected(decoded.error());
            }
            item = *decoded;
        }
        return value;
    } else if constexpr (is_wire_message_v<CleanT>) {
        constexpr auto &fields = Generated::Wire::FieldList<CleanT>::fields;
        using FieldsT = decay_field_t<decltype(fields)>;
        return decode_fields<CleanT>(
            fields, cursor,
            std::make_index_sequence<std::tuple_size_v<FieldsT>>{});
    } else if constexpr (is_transparent_value_wrapper_v<CleanT>) {
        auto decoded = decode_value<value_member_t<CleanT>>(cursor);
        if (!decoded) {
            return std::unexpected(decoded.error());
        }
        CleanT value{};
        value.value = *decoded;
        return value;
    } else {
        static_assert(always_false<CleanT>::value,
                      "Unsupported wire field type");
        return std::unexpected(error_code(CoreError::InvalidArgument));
    }
}

} // namespace wire

template <typename T> struct Codec {
    using Reader = wire::Reader;
    using SpanReader = wire::SpanReader;

    static_assert(is_wire_codec_type_v<T>,
                  "No generated wire metadata or transparent value member for "
                  "T");

    static constexpr size_t encodedSize() { return wire::encoded_size<T>(); }

    static ReturnCode encode(const T &value, std::span<std::byte> out) {
        if (out.size() < encodedSize()) {
            return wire::error_code(CoreError::Overflow);
        }

        size_t offset = 0;
        auto ret = wire::encode_value(value, out, offset);
        if (!ret.ok()) {
            return ret;
        }
        if (offset != encodedSize()) {
            return wire::error_code(CoreError::InvalidState);
        }
        return wire::ok_code();
    }

    static std::expected<T, ReturnCode> decode(Reader reader) {
        if (!reader.valid()) {
            return std::unexpected(
                wire::error_code(CoreError::InvalidArgument));
        }
        if (reader.size < encodedSize()) {
            return std::unexpected(wire::error_code(CoreError::Underflow));
        }

        auto cursor = wire::DecodeCursor{
            .reader = reader,
            .offset = 0,
        };
        auto value = wire::decode_value<T>(cursor);
        if (!value) {
            return std::unexpected(value.error());
        }
        if (cursor.offset != encodedSize()) {
            return std::unexpected(wire::error_code(CoreError::InvalidState));
        }
        return *value;
    }

    static std::expected<T, ReturnCode>
    decode(std::span<const std::byte> data) {
        auto spanReader = wire::SpanReader(data);
        return decode(spanReader.reader);
    }
};

} // namespace Totem::PubSubBackend::detail
