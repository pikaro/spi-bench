#include "Clock/Facade.hpp"
#include "Data/Nodes.hpp"
#include "LedDisplay/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Setups/ClockSync.hpp"
#include "Setups/Core.hpp"
#include "Setups/PubSubNetwork.hpp"
#include "Wire/Spi/Facade.hpp"
#include "config.hpp"
#include "led_startup.hpp"

CoreSetup core{};
Totem::LedDisplay::Display ledDisplay{core.taskRegistry};
Totem::Wire::Spi::Slave spiSlave{core.taskRegistry};
Totem::Clock::Clock clockSlave{Totem::Clock::Clock::Role::Slave};
ClockSyncSetup<Totem::Wire::Spi::Slave> clockSync{clockSlave, spiSlave};
PubSubNetworkSpiEdgeSetup<Totem::Wire::Spi::Slave, gpuNodeName> pubSubNetwork{
    core.taskRegistry, spiSlave};
LedStartup ledStartup;

void setup() {
    if constexpr (ownsLedOutputGate) {
        ABORT_IF_ERR_BEGIN(ledStartup.holdOutputGateDisabled(ledOutputGatePin));
    }
    ABORT_IF_ERR_BEGIN(core.beginStatusLedEarly(statusLedConfig));
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();

    ABORT_IF_ERR_BEGIN(spiSlave.begin(spiSlaveConfig));
    pubSubNetwork.setup();
    ABORT_IF_ERR_BEGIN(ledDisplay.begin(ledDisplayConfig));
    ABORT_IF_ERR_BEGIN(ledDisplay.beginPresentStrobe(
        ledPresentStrobeInputPin, ledPresentStrobeInputPull));
    ABORT_IF_ERR_BEGIN(ledDisplay.subscribePubSub());
    ABORT_IF_ERR_BEGIN(ledStartup.begin(ownsLedOutputGate));

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
        REPORT_IF_ERR(core.work(nowMs), "Core work failed");
        REPORT_IF_ERR(clockSync.work(nowMs), "Clock sync work failed");
        REPORT_IF_ERR(ledStartup.work(nowMs), "GPU LED startup work failed");

        ::platform::delay(::platform::ms_to_ticks(1));
    }
}
