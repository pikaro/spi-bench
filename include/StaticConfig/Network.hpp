#pragma once

#include <cstddef>
#include <cstdint>

namespace Totem::StaticConfig::Network {

inline constexpr std::size_t diagPacketBytes = 192;
inline constexpr uint32_t defaultSocketTimeoutMs = 3000;
inline constexpr uint32_t maxCommandSocketTimeoutMs = 3000;
inline constexpr int tcpListenBacklog = 1;

} // namespace Totem::StaticConfig::Network
