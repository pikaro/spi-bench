#include "Clock/Facade.hpp"
#include "Data/Nodes.hpp"
#include "LedDisplay/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Setups/ClockSync.hpp"
#include "Setups/Core.hpp"
#include "Setups/PubSubStarTest.hpp"
#include "Wire/Spi/Facade.hpp"
#include "config.hpp"

CoreSetup core{};
Totem::LedDisplay::Display ledDisplay{core.taskRegistry};
Totem::Wire::Spi::Slave spiSlave{core.taskRegistry};
Totem::Clock::Clock clockSlave{Totem::Clock::Clock::Role::Slave};
ClockSyncSetup<Totem::Wire::Spi::Slave> clockSync{clockSlave, spiSlave};
constexpr auto gpuStarRole() {
    using Role = PubSubStarSpiEdgeSetup<Totem::Wire::Spi::Slave>::Role;
    if constexpr (gpuNodeName == Totem::Data::NodeName::GPUNode1) {
        return Role::GPU1;
    }
    return Role::GPU0;
}
PubSubStarSpiEdgeSetup<Totem::Wire::Spi::Slave> pubSubStar{
    core.taskRegistry, spiSlave, gpuStarRole()};

void setup() {
    ABORT_IF_ERR_BEGIN(core.beginStatusLedEarly(statusLedConfig));
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();

    ABORT_IF_ERR_BEGIN(spiSlave.begin(spiSlaveConfig));
    pubSubStar.setup();
    ABORT_IF_ERR_BEGIN(ledDisplay.begin());
    ABORT_IF_ERR_BEGIN(ledDisplay.subscribePubSub());

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
        (void)pubSubStar.work(nowMs);
        (void)clockSync.work(nowMs);

        ::platform::delay(::platform::ms_to_ticks(1));
    }
}
