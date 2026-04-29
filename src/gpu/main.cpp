#include "Clock/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Setups/Core.hpp"
#include "Wire/Spi/Facade.hpp"
#include "config.hpp"
#include <cstdint>

CoreSetup core{};
Totem::Wire::Spi::Slave spiSlave{core.taskRegistry};
Totem::Clock::Clock clockSlave{Totem::Clock::Clock::Role::Slave};

void setup() {
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();

    ABORT_IF_ERR_BEGIN(spiSlave.begin(spiSlaveConfig));

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

        ::platform::delay(::platform::ms_to_ticks(1));
        epoch++;
    }
}
