#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Services/Clock.hpp"
#include "Setups/Core.hpp"
#include "Setups/PubSubRs485Test.hpp"
#include "Setups/PubSubTest.hpp"

#include "Clock/Facade.hpp"
#include "Wire/Rs485/Facade.hpp"
#include <cstdint>

CoreSetup core{};

Totem::Wire::Rs485::Master rs485master{core.taskRegistry};
Totem::Clock::Clock clockMaster{Totem::Clock::Clock::Role::Master};
PubSubRs485TestSetup<Totem::Wire::Rs485::Master> pubSubRs485{
    core.taskRegistry, rs485master,
    PubSubRs485TestSetup<Totem::Wire::Rs485::Master>::Role::Master};
bool rs485Started = false;

void setup() {
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();
    _log_i("Core setup complete");

    ClockService::set(clockMaster);

    ABORT_IF_ERR_BEGIN(
        rs485master.begin({.uartConfig = {.uartNumber = 1,
                                          .pins = {
                                              .txPin = ::platform::Pin::GPIO12,
                                              .rxPin = ::platform::Pin::GPIO13,
                                          }},
                            .attentionPin = ::platform::Pin::GPIO1}));

    ABORT_IF_ERR(clockMaster.registerHandler(rs485master),
                 "Failed to register RS485 clock handler");

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

        ::platform::delay(::platform::ms_to_ticks(1));
        epoch++;
    }
}
