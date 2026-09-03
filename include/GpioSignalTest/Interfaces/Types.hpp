#pragma once

#include "Platform/Hardware.hpp"
#include <cstdint>

namespace Totem::GpioSignalTest {

enum class Role : uint8_t {
    Producer,
    Consumer,
};

struct Status {
    Role role = Role::Consumer;
    Pin pin{};
    bool level = false;
    bool measurementValid = false;
    uint32_t risingEdges = 0;
    uint32_t fallingEdges = 0;
    uint32_t duplicateEdges = 0;
    uint32_t timerErrors = 0;
    uint32_t periodUs = 0;
    uint32_t minimumPeriodUs = 0;
    uint32_t maximumPeriodUs = 0;
    uint32_t highTimeUs = 0;
    uint32_t lowTimeUs = 0;
    uint32_t frequencyMilliHz = 0;
    uint16_t dutyPartsPerThousand = 0;
    uint32_t lastEdgeAgeUs = 0;
};

} // namespace Totem::GpioSignalTest
