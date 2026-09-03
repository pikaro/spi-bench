#include "AudioFft/Facade.hpp"
#include "AudioFft/Interfaces/Types.hpp"
#include "Clock/Facade.hpp"
#include "Data/Peripherals.hpp"
#include "LedPwm/Facade.hpp"
#include "LedPwm/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Setups/ClockSync.hpp"
#include "Setups/Core.hpp"
#include "Setups/PubSubNetwork.hpp"
#include "Types/Error.hpp"
#include "Wire/I2C/Facade.hpp"
#include "Wire/Spi/Facade.hpp"
#include "audio_pubsub.hpp"
#include "config.hpp"
#include <cstdint>

CoreSetup core{};
Totem::Clock::Clock clockSlave{Totem::Clock::Clock::Role::Slave};
Totem::LedPwm::LedPwm ledPwm{core.taskRegistry};
Totem::Wire::I2C::Master i2cMaster{};
Totem::Wire::I2C::Ssd1306Display fftDisplay{i2cMaster};
MediaAudioSourceType audioSource{};
Totem::AudioFft::FftAnalyzer fftAnalyzer{core.taskRegistry, audioSource};
Totem::AudioFft::FftDisplay fftDisplayVisualizer{core.taskRegistry, fftAnalyzer,
                                                 fftDisplay};

Totem::Wire::Spi::Slave spiSlave{core.taskRegistry};
ClockSyncSetup<Totem::Wire::Spi::Slave> clockSync{clockSlave, spiSlave};
PubSubNetworkSpiEdgeSetup<Totem::Wire::Spi::Slave, Totem::Data::NodeName::Media>
    pubSubNetwork{core.taskRegistry, spiSlave};

namespace {

class PeakIndicatorLed {
  public:
    ReturnCode bind(Totem::LedPwm::LedContext context) {
        _context = context;
        _bound = true;
        return OK();
    }

    static ReturnCode onPeak(void *owner,
                             const Totem::AudioFft::PeakResult &event) {
        auto *self = static_cast<PeakIndicatorLed *>(owner);
        FAIL_IF_NULL(self, ERR(CoreError, InvalidArgument),
                     "Peak indicator owner is null");
        return self->pulse(event);
    }

  private:
    ReturnCode pulse(const Totem::AudioFft::PeakResult & /*unused*/) {
        FAIL_IF(!_bound, ERR(CoreError, InvalidState),
                "Peak indicator LED is not bound");
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

PeakIndicatorLed peakIndicatorLed{};

} // namespace

void setup() {
    ABORT_IF_ERR_BEGIN(core.beginStatusLedEarly(statusLedConfig));
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();
    _log_i("Core setup complete");

    ABORT_IF_ERR_BEGIN(ledPwm.begin(ledPwmConfig));
    ABORT_IF_UNEXPECTED(peakLedContext,
                        ledPwm.getLedContext(PeripheralLed::PeakIndicator),
                        "Failed to get peak indicator LED context");
    ABORT_IF_ERR(peakIndicatorLed.bind(peakLedContext),
                 "Failed to bind peak indicator LED");
    fftAnalyzerConfig.peakIndicator = {
        .owner = &peakIndicatorLed,
        .callback = PeakIndicatorLed::onPeak,
    };

    if constexpr (enableFftDebugDisplay) {
        ABORT_IF_ERR_BEGIN(i2cMaster.begin(i2cMasterConfig));
        ABORT_IF_ERR_BEGIN(fftDisplay.begin(fftDisplayConfig));
        ABORT_IF_ERR_BEGIN(
            fftDisplayVisualizer.begin(fftDisplayVisualizerConfig));
    }

    ABORT_IF_ERR_BEGIN(spiSlave.begin(spiSlaveConfig));
    pubSubNetwork.setup();
    ABORT_IF_ERR(MediaAudioPubSub::begin(fftAnalyzer),
                 "Failed to begin media audio PubSub bridge");
    ABORT_IF_ERR_BEGIN(beginMediaAudioSource(audioSource));
    ABORT_IF_ERR_BEGIN(fftAnalyzer.begin(fftAnalyzerConfig));

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
        REPORT_IF_ERR(MediaAudioPubSub::work(),
                      "Media audio PubSub work failed");
        ::platform::delay(::platform::ms_to_ticks(1));
    }
}
