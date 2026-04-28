#pragma once

#include "Types/Uart.hpp"

namespace Totem::Wire::Rs485::detail {

struct MasterConfig {
    UartConfig uartConfig;

    [[nodiscard]] bool validate() const { return uartConfig.validate(); }
};

struct SlaveConfig {
    UartConfig uartConfig;

    [[nodiscard]] bool validate() const { return uartConfig.validate(); }
};

} // namespace Totem::Wire::Rs485::detail
