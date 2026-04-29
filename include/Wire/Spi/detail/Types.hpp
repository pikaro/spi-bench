#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include <cstddef>
#include <cstdint>
#include <span>

namespace Totem::Wire::Spi::detail {

static constexpr LogComponent logComponent = LogComponent::Spi;

struct DeviceHandle {
    static constexpr uint8_t invalidIndex = 0xFF;

    uint8_t index = invalidIndex;

    [[nodiscard]] bool valid() const { return index != invalidIndex; }

    static constexpr DeviceHandle invalid() { return {}; }
};

struct Transfer {
    std::span<const std::byte> txBuffer;
    std::span<std::byte> rxBuffer;
    uint32_t timeoutMs = 100;

    [[nodiscard]] bool validate() const {
        return !txBuffer.empty() || !rxBuffer.empty();
    }

    [[nodiscard]] size_t clockedSize() const {
        return txBuffer.size() > rxBuffer.size() ? txBuffer.size()
                                                 : rxBuffer.size();
    }
};

struct TransferResult {
    size_t bytesTransferred = 0;
};

} // namespace Totem::Wire::Spi::detail
