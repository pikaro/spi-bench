#pragma once
#include "magic_enum/magic_enum.hpp"
#include <cassert>
#include <cstdint>

enum class ErrorDomain : uint8_t {
    Core,
    Lifecycle,
    PubSub,
    Spi,
    Command,
};

enum class CoreError : uint8_t {
    Unknown = 0,
    Ok,
    Abort,
    InvalidArgument,
    OutOfMemory,
    NotFound,
    OperationFailed,
    AlreadyExists,
    Timeout,
    Unexpected,
    InvalidState,
    InvalidData,
    Overflow,
    Underflow,
};

enum class PubSubError : uint8_t {
    Unknown = 0,
    Ok,
};

enum class SpiError : uint8_t {
    Unknown = 0,
    Ok,
    CommunicationFailure,
    InvalidResponse,
};

enum class LifecycleError : uint8_t {
    Unknown = 0,
    Ok,
    Active,
    NotActive,
    InvalidState,
};

enum class CommandError : uint8_t {
    Unknown = 0,
    Ok,
    SyntaxError,
    TooLong,
};

template <typename Enum> constexpr const char *error_name(uint8_t value) {
    return magic_enum::enum_name(static_cast<Enum>(value)).data();
}

struct [[nodiscard]] ReturnCode {
    struct FormatView {
        uint8_t code;
        const char *domain;
        const char *name;
    };

    explicit operator bool() const { return ok(); }
    bool operator!() const { return !ok(); }
    bool operator==(const ReturnCode &other) const {
        return (domain == other.domain) && (code == other.code);
    }
    bool operator!=(const ReturnCode &other) const { return !(*this == other); }

    template <typename Enum>
    static constexpr ReturnCode from(ErrorDomain domain, Enum err) {
        static_assert(std::is_enum_v<Enum>,
                      "ReturnCode::from requires an enum type");
        return {
            .domain = domain,
            .code = static_cast<uint8_t>(err),
        };
    }

    static constexpr ReturnCode from(CoreError err) {
        return from<CoreError>(ErrorDomain::Core, err);
    }
    static constexpr ReturnCode from(PubSubError err) {
        return from<PubSubError>(ErrorDomain::PubSub, err);
    }
    static constexpr ReturnCode from(SpiError err) {
        return from<SpiError>(ErrorDomain::Spi, err);
    }
    static constexpr ReturnCode from(LifecycleError err) {
        return from<LifecycleError>(ErrorDomain::Lifecycle, err);
    }
    static constexpr ReturnCode from(CommandError err) {
        return from<CommandError>(ErrorDomain::Command, err);
    }

    [[nodiscard]] constexpr const char *name() const {
        switch (domain) {
        case ErrorDomain::Core:
            return error_name<CoreError>(code);
        case ErrorDomain::PubSub:
            return error_name<PubSubError>(code);
        case ErrorDomain::Spi:
            return error_name<SpiError>(code);
        case ErrorDomain::Lifecycle:
            return error_name<LifecycleError>(code);
        case ErrorDomain::Command:
            return error_name<CommandError>(code);
        default:
            assert(false && "ReturnCode contains invalid domain");
            return "InvalidDomain";
        }
    }

    void combine(const ReturnCode &other) {
        if (!other.ok()) {
            *this = other;
        }
    }

    [[nodiscard]] constexpr bool ok() const { return code == 1; }

    [[nodiscard]] constexpr FormatView format() const {
        auto domainName = magic_enum::enum_name(domain);
        return {
            .code = code,
            // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
            .domain = domainName.data(),
            // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
            .name = name(),
        };
    }

    ErrorDomain domain{ErrorDomain::Core};
    uint8_t code{0};
};
