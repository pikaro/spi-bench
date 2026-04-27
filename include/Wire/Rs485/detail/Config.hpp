#pragma once

#include "Platform/PlatformSelect.hpp"

namespace Totem::Wire::Rs485::detail {

struct BaseConfig {
    Pin txPin;
    Pin rxPin;
};

struct MasterConfig {
    BaseConfig base;
};

struct SlaveConfig {
    BaseConfig base;
};

} // namespace Totem::Wire::Rs485::detail
