#pragma once

#include <cstddef>

namespace Totem::StaticConfig::Bluetooth {

inline constexpr size_t maxDrivers = 4;
inline constexpr size_t maxSubscriptions = 4;
inline constexpr size_t notificationQueueSize = 8;
inline constexpr size_t maxNotificationPayloadBytes = 32;

} // namespace Totem::StaticConfig::Bluetooth
