#include "Clock/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Platform/Gpio.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Services/Clock.hpp"
#include "Setups/Core.hpp"
#include "Setups/PubSubStarTest.hpp"
#include "Wheel/Interfaces/Wire.hpp"
#include "Wire/Rs485/Facade.hpp"
#include "Wire/Spi/Facade.hpp"
#include "config.hpp"
#include <cstdint>

CoreSetup core{};

Totem::Wire::Rs485::Master rs485master{core.taskRegistry};
Totem::Wire::Spi::Master spiMasterGpu0{core.taskRegistry};
Totem::Wire::Spi::Master spiMasterGpu1{core.taskRegistry};
Totem::Wire::Spi::Master spiMasterLowSpeed{core.taskRegistry};
Totem::Clock::Clock clockMaster{Totem::Clock::Clock::Role::Master};
PubSubMultiSpiStarMasterSetup<Totem::Wire::Spi::Master,
                              Totem::Wire::Spi::Master,
                              Totem::Wire::Rs485::Master>
    pubSubStar{core.taskRegistry, spiMasterLowSpeed, spiMasterGpu0,
               spiMasterGpu1, rs485master};
platform::Gpio levelShifterEnable;

void setup() {
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();
    _log_i("Core setup complete");

    ClockService::set(clockMaster);

    _log_d("Current clock time: %uus", clockMaster.nowUs());

    ABORT_IF_ERR_BEGIN(rs485master.begin(rs485MasterConfig));

    ABORT_IF_ERR_BEGIN(spiMasterGpu0.begin(spiMasterBusHighSpeedConfig))
    ABORT_IF_ERR_BEGIN(spiMasterGpu1.begin(spiMasterBusHighSpeedGpu1Config))
    ABORT_IF_ERR_BEGIN(spiMasterLowSpeed.begin(spiMasterBusLowSpeedConfig))

    ABORT_IF_ERR(clockMaster.registerHandler(spiMasterLowSpeed),
                 "Failed to register low-speed SPI clock handler");
    ABORT_IF_ERR(clockMaster.registerHandler(spiMasterGpu0),
                 "Failed to register GPU0 SPI clock handler");
    ABORT_IF_ERR(clockMaster.registerHandler(spiMasterGpu1),
                 "Failed to register GPU1 SPI clock handler");

    ABORT_IF_ERR(clockMaster.registerHandler(rs485master),
                 "Failed to register RS485 clock handler");

    pubSubStar.setup();

    ABORT_IF_ERR(levelShifterEnable.initOutput(levelShifterEnablePin),
                 "Failed to initialize level shifter enable GPIO");

    _log_i("Setup complete");
}

extern "C" {
void app_main(void);
}

uint32_t epoch = 0;

void app_main() {
    setup();

    ABORT_IF_ERR(levelShifterEnable.setLevel(false),
                 "Failed to disable level shifter at startup");

    for (;;) {
        const auto nowMs = ::platform::get_time();
        (void)core.work(nowMs);
        (void)pubSubStar.work(nowMs);

        ::platform::delay(::platform::ms_to_ticks(1));
        epoch++;
    }
}
