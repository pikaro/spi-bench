#pragma once

#include <cstdint>

enum class ErrorDomain : uint8_t {
    Core,
};

enum class CoreError : uint8_t {
    Unknown = 0,
    Ok,
    InvalidArgument,
    InvalidState,
    OutOfMemory,
};

struct [[nodiscard]] ReturnCode {
    explicit operator bool() const { return ok(); }
    bool operator!() const { return !ok(); }
    [[nodiscard]] constexpr bool ok() const { return code == 1; }

    ErrorDomain domain{ErrorDomain::Core};
    uint8_t code{0};
};

[[nodiscard]] constexpr ReturnCode OK() {
    return ReturnCode{.domain = ErrorDomain::Core,
                      .code = static_cast<uint8_t>(CoreError::Ok)};
}
