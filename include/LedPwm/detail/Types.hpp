#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include <cstdint>

namespace Totem::LedPwm::detail {

static constexpr LogComponent logComponent = LogComponent::LedPwm;

struct LedHandle {
    uint8_t idx;
};

struct LedContext {
    void *ctx;
    LedHandle led;
    ReturnCode (*command)(void *ctx, const void *cmd);
};

} // namespace Totem::LedPwm::detail
