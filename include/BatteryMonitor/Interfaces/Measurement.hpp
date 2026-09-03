#pragma once

#include <cstdint>

namespace Totem::BatteryMonitor {

/** Sensor-independent battery measurement captured at one point in time. */
struct BatteryMeasurement {
    uint32_t capturedAtMs = 0;
    uint32_t voltageMillivolts = 0;
    int32_t currentMicroamps = 0;
    int32_t powerMilliwatts = 0;
};

} // namespace Totem::BatteryMonitor
