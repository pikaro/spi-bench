#pragma once

#include <cstdint>

namespace Totem::TaskControllerRegistry {

enum class TaskSourceKind : uint8_t {
    Unknown = 0,
    ManagedController,
    PlatformSystem,
    ExternalLibrary,
};

struct TaskSourceCapability {
    static constexpr uint32_t None = 0;
    static constexpr uint32_t Reap = 1U << 0;
    static constexpr uint32_t Managed = 1U << 1;
};

} // namespace Totem::TaskControllerRegistry
