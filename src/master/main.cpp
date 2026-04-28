#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Services/Clock.hpp"
#include "Setups/Core.hpp"
#include "Setups/PubSubTest.hpp"

#include "Clock/Facade.hpp"
#include "Wire/Rs485/Facade.hpp"

CoreSetup core{};

Totem::Wire::Rs485::Master rs485master{core.taskRegistry};
Totem::Clock::Clock clockMaster{Totem::Clock::Clock::Role::Master};
bool rs485Started = false;

void setup() {
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();
    _log_i("Core setup complete");

    ClockService::set(clockMaster);

    const auto rs485BeginResult =
        rs485master.begin({.uartConfig = {.uartNumber = 1,
                                          .pins = {
                                              .txPin = ::platform::Pin::GPIO12,
                                              .rxPin = ::platform::Pin::GPIO13,
                                          }}});
    if (!rs485BeginResult.ok()) {
        _log_e("RS485 master disabled: " ERR_FMT, ERR_ARG(rs485BeginResult));
    } else {
        rs485Started = true;
    }

    if (rs485Started) {
        const auto clockRegisterResult =
            clockMaster.registerHandler(rs485master);
        if (!clockRegisterResult.ok()) {
            _log_e("Clock sync server disabled: " ERR_FMT,
                   ERR_ARG(clockRegisterResult));
        }
    }

    _log_i("Setup complete");
}

extern "C" {
void app_main(void);
}

void app_main() {
    setup();
    for (;;) {
        const auto nowMs = ::platform::get_time();
        (void)core.work(nowMs);
        ::platform::delay(::platform::ms_to_ticks(publishIntervalMs));
    }
}
