#pragma once

#include "StaticConfig/Stacks.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include <cstddef>
#include <cstdint>

namespace Totem::StaticConfig::PubSubUdp {

inline constexpr bool enabled = true;
inline constexpr uint16_t localPort = 2026;
inline constexpr size_t rxQueueDepth = 8;
inline constexpr uint32_t receiveTimeoutMs = 250;
inline constexpr uint32_t keepaliveIntervalMs = 1000;
inline constexpr uint32_t peerTimeoutMs = 5000;
inline constexpr size_t txBurstLimit = 1;

inline constexpr Totem::TaskController::Config task{
    .name = "PubSubUdpTask",
    .priority = 3,
    .core = Totem::TaskController::Config::CorePreference::any(),
    .stackSize = Totem::StaticConfig::TaskStacks::pubSubUdp,
    .intervalMs = 1,
    .noCatchup = true,
    .useWdt = true,
    .useNotify = true,
    .endTimeoutMs = 2000,
    .autoRestart = false,
};

} // namespace Totem::StaticConfig::PubSubUdp
