#pragma once

#include "TaskControllerRegistry/Interfaces/ITaskSource.hpp"
#include "TaskControllerRegistry/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include <cstdint>

namespace Totem::TaskController {

struct IRegistry {
    using SourceKey = TaskControllerRegistry::SourceKey;

    virtual ~IRegistry() = default;

    virtual ReturnCode
    registerSource(const SourceKey &sourceKey,
                   TaskControllerRegistry::ITaskSource &source,
                   TaskControllerRegistry::TaskSourceInfo info) = 0;
    virtual ReturnCode deregisterSource(const SourceKey &sourceKey) = 0;
    virtual ReturnCode registerManagedTaskHandle(uintptr_t handle) = 0;
    virtual ReturnCode deregisterManagedTaskHandle(uintptr_t handle) = 0;
};

} // namespace Totem::TaskController
