#pragma once

#include "StaticConfig/Network.hpp"
#include <cstddef>
#include <cstdint>

namespace Totem::Network {

struct ReceiveConfig {
    uint32_t timeoutMs = Totem::StaticConfig::Network::defaultSocketTimeoutMs;
    std::size_t maxBytes = Totem::StaticConfig::Network::diagPacketBytes;

    [[nodiscard]] constexpr bool validate() const { return maxBytes > 0; }
};

} // namespace Totem::Network
