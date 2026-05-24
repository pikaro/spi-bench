#pragma once

#include "Bluetooth/Interfaces/Device.hpp"
#include "StaticConfig/Bluetooth.hpp"
#include "StaticConfig/Stacks.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace Totem::Bluetooth {

struct Config {
    bool enabled = true;
    std::array<IDeviceDriver *, StaticConfig::Bluetooth::maxDrivers> drivers{};
    size_t driverCount = 0;

    bool activeScan = true;
    bool filterDuplicates = true;
    uint16_t scanInterval = 160;
    uint16_t scanWindow = 80;
    uint32_t scanDurationMs = 5000;
    uint32_t connectTimeoutMs = 5000;

    TaskController::Config task = {
        .name = "Bluetooth",
        .priority = 3,
        .stackSize = StaticConfig::TaskStacks::bluetooth,
        .intervalMs = 1000,
        .noCatchup = true,
        .useNotify = true,
        .notifyExpectTimeout = true,
        .notifyTimeoutMs = 1000,
    };

    [[nodiscard]] bool validate() const {
        if (!enabled) {
            return true;
        }
        if (!task.validate() || driverCount == 0 ||
            driverCount > drivers.size() || scanInterval == 0 ||
            scanWindow == 0 || scanWindow > scanInterval ||
            scanDurationMs == 0 || connectTimeoutMs == 0) {
            return false;
        }
        for (size_t i = 0; i < driverCount; ++i) {
            const auto *driver = drivers[i];
            if (driver == nullptr || driver->serviceUuid() == nullptr ||
                driver->serviceUuid()[0] == '\0' ||
                driver->characteristicUuid() == nullptr ||
                driver->characteristicUuid()[0] == '\0') {
                return false;
            }
        }
        return true;
    }
};

} // namespace Totem::Bluetooth
