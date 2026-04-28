#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Setups/Core.hpp"
#include "Setups/PubSubTest.hpp"

#include "Clock/Facade.hpp"
#include "Wire/Rs485/Facade.hpp"

CoreSetup core{};
Totem::Wire::Rs485::Slave rs485Slave{core.taskRegistry};
Totem::Clock::Clock clockSlave{Totem::Clock::Clock::Role::Slave};
bool rs485Started = false;

void setup() {
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();
    _log_i("Core setup complete");

    const auto rs485BeginResult =
        rs485Slave.begin({.uartConfig = {.uartNumber = 1,
                                         .pins = {
                                             .txPin = ::platform::Pin::GPIO6,
                                             .rxPin = ::platform::Pin::GPIO5,
                                         }}});
    if (!rs485BeginResult.ok()) {
        _log_e("RS485 slave disabled: " ERR_FMT, ERR_ARG(rs485BeginResult));
    } else {
        rs485Started = true;
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
        if (rs485Started && rs485Slave.ready() && !clockSlave.synced() &&
            !clockSlave.syncing()) {
            const auto syncResult = clockSlave.sync(rs485Slave);
            if (!syncResult.ok()) {
                _log_e("Clock sync request failed: " ERR_FMT,
                       ERR_ARG(syncResult));
            } else {
                _log_i("Clock sync requested");
            }
        }
        ::platform::delay(::platform::ms_to_ticks(publishIntervalMs));
    }
}
