#include "Audio/Facade.hpp"
#include "Clock/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Setups/Core.hpp"
#include "Setups/PubSubStarTest.hpp"
#include "Wire/Spi/Facade.hpp"
#include "config.hpp"
#include <cstdint>

CoreSetup core{};
Totem::Clock::Clock clockSlave{Totem::Clock::Clock::Role::Slave};
Totem::Audio::I2SSource i2sSource{};
Totem::Audio::FftAnalyzer fftAnalyzer{core.taskRegistry, i2sSource};
Totem::Wire::Spi::Slave spiSlave{core.taskRegistry};
PubSubStarSpiEdgeSetup<Totem::Wire::Spi::Slave> pubSubStar{
    core.taskRegistry, spiSlave,
    PubSubStarSpiEdgeSetup<Totem::Wire::Spi::Slave>::Role::Media};

void setup() {
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();
    _log_i("Core setup complete");

    ABORT_IF_ERR_BEGIN(i2sSource.begin(i2sSourceConfig));
    ABORT_IF_ERR_BEGIN(fftAnalyzer.begin(fftAnalyzerConfig));
    ABORT_IF_ERR_BEGIN(spiSlave.begin(spiSlaveConfig));

    pubSubStar.setup();

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
        (void)pubSubStar.work(nowMs);

        if (spiSlave.ready() && (!clockSlave.synced() || epoch % 10000 == 0) &&
            !clockSlave.syncing()) {
            const auto syncResult = clockSlave.sync(spiSlave);
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
