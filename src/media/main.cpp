#include "Audio/Facade.hpp"
#include "Audio/Interfaces/Types.hpp"
#include "Clock/Facade.hpp"
#include "Data/Peripherals.hpp"
#include "LedPwm/Facade.hpp"
#include "LedPwm/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Setups/ClockSync.hpp"
#include "Setups/Core.hpp"
#include "Setups/PubSubStarTest.hpp"
#include "Types/Error.hpp"
#include "Wire/I2C/Facade.hpp"
#include "Wire/Spi/Facade.hpp"
#include "config.hpp"
#include <cstdint>

CoreSetup core{};
Totem::Clock::Clock clockSlave{Totem::Clock::Clock::Role::Slave};
Totem::LedPwm::LedPwm ledPwm{core.taskRegistry};
Totem::Wire::I2C::Master i2cMaster{};
Totem::Wire::I2C::Ssd1306Display fftDisplay{i2cMaster};
Totem::Audio::AudioSource audioSource{};
Totem::Audio::FftAnalyzer fftAnalyzer{core.taskRegistry, audioSource};
Totem::Audio::FftDisplay fftDisplayVisualizer{core.taskRegistry, fftAnalyzer,
                                              fftDisplay};

Totem::Wire::Spi::Slave spiSlave{core.taskRegistry};
ClockSyncSetup<Totem::Wire::Spi::Slave> clockSync{clockSlave, spiSlave};
PubSubStarSpiEdgeSetup<Totem::Wire::Spi::Slave> pubSubStar{
    core.taskRegistry, spiSlave,
    PubSubStarSpiEdgeSetup<Totem::Wire::Spi::Slave>::Role::Media};

namespace {

class BeatIndicatorLed {
  public:
    ReturnCode bind(Totem::LedPwm::LedContext context) {
        _context = context;
        _bound = true;
        return OK();
    }

    static ReturnCode onBeat(void *owner,
                             const Totem::Audio::BeatResult &event) {
        auto *self = static_cast<BeatIndicatorLed *>(owner);
        FAIL_IF_NULL(self, ERR(CoreError, InvalidArgument),
                     "Beat indicator owner is null");
        return self->pulse(event);
    }

  private:
    ReturnCode pulse(const Totem::Audio::BeatResult & /*unused*/) {
        FAIL_IF(!_bound, ERR(CoreError, InvalidState),
                "Beat indicator LED is not bound");
        auto command = Totem::LedPwm::LedCommand::pulse({
            .peak = Totem::LedPwm::Brightness::full(),
            .riseMs = 0,
            .holdMs = 20,
            .fallMs = 90,
            .curve = Totem::LedPwm::Curve::SmoothStep,
        });
        return command.run(_context);
    }

    Totem::LedPwm::LedContext _context{};
    bool _bound = false;
};

BeatIndicatorLed beatIndicatorLed{};

} // namespace

void setup() {
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();
    _log_i("Core setup complete");

    ABORT_IF_ERR_BEGIN(ledPwm.begin(ledPwmConfig));
    ABORT_IF_UNEXPECTED(beatLedContext,
                        ledPwm.getLedContext(PeripheralLed::BeatIndicator),
                        "Failed to get beat indicator LED context");
    ABORT_IF_ERR(beatIndicatorLed.bind(beatLedContext),
                 "Failed to bind beat indicator LED");
    fftAnalyzerConfig.beatIndicator = {
        .owner = &beatIndicatorLed,
        .callback = BeatIndicatorLed::onBeat,
    };

    if constexpr (enableFftDebugDisplay) {
        ABORT_IF_ERR_BEGIN(i2cMaster.begin(i2cMasterConfig));
        ABORT_IF_ERR_BEGIN(fftDisplay.begin(fftDisplayConfig));
        ABORT_IF_ERR_BEGIN(
            fftDisplayVisualizer.begin(fftDisplayVisualizerConfig));
    }

    ABORT_IF_ERR_BEGIN(spiSlave.begin(spiSlaveConfig));
    pubSubStar.setup();
    ABORT_IF_ERR_BEGIN(audioSource.begin(audioSourceConfig));
    ABORT_IF_ERR_BEGIN(fftAnalyzer.begin(fftAnalyzerConfig));

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
        (void)pubSubStar.work(nowMs);
        (void)clockSync.work(nowMs);
        ::platform::delay(::platform::ms_to_ticks(1));
    }
}
