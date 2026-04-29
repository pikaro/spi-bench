#include "Clock/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Services/Clock.hpp"
#include "Setups/Core.hpp"
// #include "Setups/PubSubRs485Test.hpp"
// #include "Wire/Rs485/Facade.hpp"
#include "Wire/Spi/Facade.hpp"
#include "config.hpp"
#include <cstdint>

CoreSetup core{};

// Totem::Wire::Rs485::Master rs485master{core.taskRegistry};
Totem::Wire::Spi::Master spiMasterHighSpeed{core.taskRegistry};
Totem::Wire::Spi::Master spiMasterLowSpeed{core.taskRegistry};
Totem::Clock::Clock clockMaster{Totem::Clock::Clock::Role::Master};
// PubSubRs485TestSetup<Totem::Wire::Rs485::Master> pubSubRs485{
//     core.taskRegistry, rs485master,
//     PubSubRs485TestSetup<Totem::Wire::Rs485::Master>::Role::Master};

void setup() {
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();
    _log_i("Core setup complete");

    ClockService::set(clockMaster);

    _log_d("Current clock time: %uus", clockMaster.nowUs());

    // ABORT_IF_ERR_BEGIN(rs485master.begin(rs485MasterConfig));

    ABORT_IF_ERR_BEGIN(spiMasterHighSpeed.begin(spiMasterBusHighSpeedConfig))
    ABORT_IF_ERR_BEGIN(spiMasterLowSpeed.begin(spiMasterBusLowSpeedConfig))

    // ABORT_IF_ERR(clockMaster.registerHandler(rs485master),
    //              "Failed to register RS485 clock handler");

    // pubSubRs485.setup();

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
        // (void)pubSubRs485.work(nowMs);

        ::platform::delay(::platform::ms_to_ticks(1));
        epoch++;
    }
}
