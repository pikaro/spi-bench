#pragma once
#include <array>
#include <cstdint>

namespace Totem::Core {

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
};

static constexpr auto coreErrorNames = std::to_array<const char *>({
    "[0] Core::Unknown",
    "[1] Core::Ok",
    "[2] Core::Abort",
    "[3] Core::InvalidArgument",
    "[4] Core::OutOfMemory",
    "[5] Core::NotFound",
    "[6] Core::OperationFailed",
    "[7] Core::AlreadyExists",
    "[8] Core::Timeout",
    "[9] Core::Unexpected",
    "[10] Core::InvalidState",
    "[11] Core::InvalidData",
    "[12] Core::Overflow",
});

enum class PubSubError : uint8_t {
    Unknown = 0,
    Ok,
};

static constexpr auto pubSubErrorNames = std::to_array<const char *>({
    "[0] PubSub::Unknown",
    "[1] PubSub::Ok",
});

enum class SpiError : uint8_t {
    Unknown = 0,
    Ok,
    CommunicationFailure,
    InvalidResponse,
};

static constexpr auto spiErrorNames = std::to_array<const char *>({
    "[0] Spi::Unknown",
    "[1] Spi::Ok",
    "[2] Spi::CommunicationFailure",
    "[3] Spi::InvalidResponse",
});

enum class LifecycleError : uint8_t {
    Unknown = 0,
    Ok,
    Active,
    NotActive,
    InvalidState,
};

static constexpr auto lifecycleErrorNames = std::to_array<const char *>({
    "[0] Lifecycle::Unknown",
    "[1] Lifecycle::Ok",
    "[2] Lifecycle::Active",
    "[3] Lifecycle::NotActive",
    "[4] Lifecycle::InvalidState",
});

enum class CommandError : uint8_t {
    Unknown = 0,
    Ok,
    SyntaxError,
    TooLong,
};

static constexpr auto commandErrorNames = std::to_array<const char *>({
    "[0] Command::Unknown",
    "[1] Command::Ok",
    "[2] Command::SyntaxError",
    "[3] Command::TooLong",
});

struct NameTable {
    const char *const *data;
    uint8_t size;
};

static constexpr auto domain_tables = std::to_array<NameTable>({
    NameTable{.data = coreErrorNames.data(),
              .size = static_cast<uint8_t>(coreErrorNames.size())},
    NameTable{.data = lifecycleErrorNames.data(),
              .size = static_cast<uint8_t>(lifecycleErrorNames.size())},
    NameTable{.data = pubSubErrorNames.data(),
              .size = static_cast<uint8_t>(pubSubErrorNames.size())},
    NameTable{.data = spiErrorNames.data(),
              .size = static_cast<uint8_t>(spiErrorNames.size())},
    NameTable{.data = commandErrorNames.data(),
              .size = static_cast<uint8_t>(commandErrorNames.size())},
});

static constexpr const char *get_error_name(ErrorDomain domain, uint8_t code) {
    const auto dom = static_cast<uint8_t>(domain);
    if (dom >= domain_tables.size()) {
        return "UnknownErrorDomain";
    }

    const NameTable tbl = domain_tables[dom];
    if (code >= tbl.size) {
        return "UnknownErrorCode";
    }

    return tbl.data[code];
}

struct [[nodiscard]] ReturnCode {
    explicit operator bool() const { return ok(); }
    bool operator!() const { return !ok(); }
    bool operator==(const ReturnCode &other) const {
        return (domain == other.domain) && (code == other.code);
    }
    bool operator!=(const ReturnCode &other) const { return !(*this == other); }

    static constexpr ReturnCode from(CoreError err) {
        return {.domain = ErrorDomain::Core, .code = static_cast<uint8_t>(err)};
    }
    static constexpr ReturnCode from(PubSubError err) {
        return {.domain = ErrorDomain::PubSub,
                .code = static_cast<uint8_t>(err)};
    }
    static constexpr ReturnCode from(SpiError err) {
        return {.domain = ErrorDomain::Spi, .code = static_cast<uint8_t>(err)};
    }
    static constexpr ReturnCode from(LifecycleError err) {
        return {.domain = ErrorDomain::Lifecycle,
                .code = static_cast<uint8_t>(err)};
    }
    static constexpr ReturnCode from(CommandError err) {
        return {.domain = ErrorDomain::Command,
                .code = static_cast<uint8_t>(err)};
    }

    [[nodiscard]] constexpr bool ok() const { return code == 1; }

    [[nodiscard]] constexpr const char *format() const {
        return get_error_name(domain, code);
    }

    ErrorDomain domain{ErrorDomain::Core};
    uint8_t code{0};
};

} // namespace Totem::Core

using Totem::Core::CommandError;
using Totem::Core::CoreError;
using Totem::Core::ErrorDomain;
using Totem::Core::LifecycleError;
using Totem::Core::PubSubError;
using Totem::Core::ReturnCode;
using Totem::Core::SpiError;
