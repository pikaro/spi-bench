#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Services/StatusLed.hpp"
#include "Setups/Core.hpp"
#include "Wifi/Facade.hpp"
#include "Wifi/detail/Commands.hpp"
#include "config.hpp"

#include "SecretStorage/Facade.hpp"

CoreSetup core{};
Totem::Wifi::Wifi wifi;

void setup() {
    ABORT_IF_ERR_BEGIN(core.beginStatusLedEarly(statusLedConfig));
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();
    _log_i("Core setup complete");

    ABORT_IF_ERR_BEGIN(wifi.begin(wifiConfig));
    ABORT_IF_ERR(Totem::Wifi::Commands::registerCommands(wifi),
                 "Failed to register WiFi commands");

    _log_i("Setup complete");
    ABORT_IF_ERR(StatusLedService::setTargetsReady(),
                 "Failed to set status LED targets-ready state");
}

extern "C" {
void app_main(void);
}

void app_main() {
    setup();
    for (;;) {
        const auto nowMs = ::platform::get_time();
        (void)core.work(nowMs);
        ::platform::delay(::platform::ms_to_ticks(1));
    }
}
