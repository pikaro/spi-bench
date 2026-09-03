#pragma once

#include <cstdint>

namespace platform {

enum class HostPin : uint8_t {
    None = 0,
};

} // namespace platform

using Pin = platform::HostPin;
