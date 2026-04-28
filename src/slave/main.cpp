#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Setups/Core.hpp"
#include "Setups/PubSubRs485Test.hpp"

#include "Clock/Facade.hpp"
#include "Wire/Rs485/Facade.hpp"
#include <cstdint>

CoreSetup core{};
Totem::Wire::Rs485::Slave rs485Slave{core.taskRegistry};
Totem::Clock::Clock clockSlave{Totem::Clock::Clock::Role::Slave};
PubSubRs485TestSetup<Totem::Wire::Rs485::Slave> pubSubRs485{
    core.taskRegistry, rs485Slave,
    PubSubRs485TestSetup<Totem::Wire::Rs485::Slave>::Role::Slave};
bool rs485Started = false;

void setup() {
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();
    _log_i("Core setup complete");

    ABORT_IF_ERR_BEGIN(
        rs485Slave.begin({.uartConfig = {.uartNumber = 1,
                                         .pins = {
                                             .txPin = ::platform::Pin::GPIO6,
                                             .rxPin = ::platform::Pin::GPIO5,
                                         }},
                           .attentionPin = ::platform::Pin::GPIO10}));

    pubSubRs485.setup();

    _log_i("Setup complete");
}

extern "C" {
void app_main(void);
}

uint32_t epoch = 0;

void app_main() {
    setup();
    for (;;) {
        const auto nowMs = ::platform::get_time();
        (void)core.work(nowMs);
        (void)pubSubRs485.work(nowMs);

        if (rs485Slave.ready() &&
            (!clockSlave.synced() || epoch % 10000 == 0) &&
            !clockSlave.syncing()) {
            const auto syncResult = clockSlave.sync(rs485Slave);
            if (!syncResult.ok()) {
                _log_e("Clock sync request failed: " ERR_FMT,
                       ERR_ARG(syncResult));
            } else {
                _log_i("Clock sync requested");
            }
        }

        ::platform::delay(::platform::ms_to_ticks(1));
        epoch++;
    }
}
