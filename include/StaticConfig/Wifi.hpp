#pragma once

#include <cstdint>

namespace Totem::StaticConfig::Wifi {

inline constexpr uint8_t defaultApChannel = 6;
// The master AP is a control link for a single client.
inline constexpr uint8_t defaultApMaxConnections = 1;
inline constexpr bool defaultStationReconnect = true;
inline constexpr uint8_t defaultStationMaxReconnectAttempts = 0;

} // namespace Totem::StaticConfig::Wifi
