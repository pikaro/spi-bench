#pragma once

#include "Base/HasLifecycle.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include "Wifi/Interfaces/Config.hpp"
#include "Wifi/Interfaces/Types.hpp"
#include "Wifi/detail/PlatformSelect.hpp"

namespace Totem::Wifi::detail {

class Wifi : public HasLifecycle<Wifi, Config> {
    friend class HasLifecycle<Wifi, Config>;
    friend struct LifecycleContract<Wifi, Config>;

  public:
    DELETE_COPY(Wifi)
    DELETE_MOVE(Wifi)

    Wifi() = default;

    static constexpr const char *name = "Wifi";

    [[nodiscard]] Status status() const { return _platform.status(); }

  private:
    ReturnCode _onBegin() { return _platform.begin(config()); }
    ReturnCode _onEnd() { return _platform.end(); }

    SelectedPlatform _platform;

    static constexpr LogComponent logComponent = LogComponent::System;
};

inline constexpr LifecycleContract<Wifi, Config> _wifi_lifecycle;

} // namespace Totem::Wifi::detail
