#pragma once

#include "Bluetooth/Interfaces/Config.hpp"
#include "Bluetooth/detail/Types.hpp"
#include "Types/Error.hpp"

namespace Totem::Bluetooth::detail::platform {

class NullCentral {
  public:
    ReturnCode begin(const Config &config, NotificationSinkBinding sink) {
        (void)config;
        (void)sink;
        return ERR(NotSupported);
    }

    ReturnCode end() { return OK(); }
};

} // namespace Totem::Bluetooth::detail::platform
