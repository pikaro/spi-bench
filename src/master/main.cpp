#include "Clock/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Network/Facade.hpp"
#include "Platform/Gpio.hpp"
#include "Platform/PlatformSelect.hpp"
#include "PubSubBackend/Transports/UdpTransport.hpp"
#include "Services/Clock.hpp"
#include "Setups/Core.hpp"
#include "Setups/PubSubNetwork.hpp"
#include "StaticConfig/PubSubUdp.hpp"
#include "Wifi/Facade.hpp"
#include "Wire/Rs485/Facade.hpp"
#include "Wire/Spi/Facade.hpp"
#include "config.hpp"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "led_bringup.hpp"
#include "orchestration.hpp"
#include <cstdint>

namespace {

class LedPresentStrobeOutput {
  public:
    ReturnCode begin(Pin pin) {
        _pin = static_cast<gpio_num_t>(static_cast<uint8_t>(pin));
        FAIL_IF_ERR_FWD(_gpio.initOutput(pin, GpioOutputMode::PushPull, false),
                        "Failed to initialize LED present strobe output GPIO");

        gptimer_config_t timerConfig{};
        timerConfig.clk_src = GPTIMER_CLK_SRC_DEFAULT;
        timerConfig.direction = GPTIMER_COUNT_UP;
        timerConfig.resolution_hz = 1000000;
        FAIL_IF_PLATFORM_FWD(gptimer_new_timer(&timerConfig, &_timer),
                             "Failed to create LED present strobe timer");

        gptimer_event_callbacks_t callbacks{};
        callbacks.on_alarm = _onAlarm;
        FAIL_IF_PLATFORM_FWD(
            gptimer_register_event_callbacks(_timer, &callbacks, this),
            "Failed to register LED present strobe timer callback");

        gptimer_alarm_config_t alarmConfig{};
        alarmConfig.alarm_count = ledPresentStrobeHalfPeriodUs;
        alarmConfig.reload_count = 0;
        alarmConfig.flags.auto_reload_on_alarm = true;
        FAIL_IF_PLATFORM_FWD(gptimer_set_alarm_action(_timer, &alarmConfig),
                             "Failed to configure LED present strobe timer");
        FAIL_IF_PLATFORM_FWD(gptimer_enable(_timer),
                             "Failed to enable LED present strobe timer");
        FAIL_IF_PLATFORM_FWD(gptimer_start(_timer),
                             "Failed to start LED present strobe timer");

        _log_i("LED present strobe output ready on pin " SV_FMT " at %lu FPS",
               MAGIC_SV_ARG(pin),
               static_cast<unsigned long>(ledPresentStrobeFps));
        return OK();
    }

  private:
    static bool _onAlarm(gptimer_handle_t /*timer*/,
                         const gptimer_alarm_event_data_t * /*event*/,
                         void *owner) {
        auto *self = static_cast<LedPresentStrobeOutput *>(owner);
        if (self == nullptr) {
            return false;
        }
        self->_level = !self->_level;
        (void)gpio_set_level(self->_pin, self->_level ? 1 : 0);
        return false;
    }

    platform::Gpio _gpio;
    gptimer_handle_t _timer = nullptr;
    gpio_num_t _pin = GPIO_NUM_NC;
    bool _level = false;
};

bool pubSubUdpNetworkReady(void *owner) {
    auto *wifiRef = static_cast<Totem::Wifi::Wifi *>(owner);
    if (wifiRef == nullptr) {
        return false;
    }
    const auto status = wifiRef->status();
    if (!status.started) {
        return false;
    }
    return status.stationIpv4.valid || status.accessPointStarted;
}

} // namespace

CoreSetup core{};

Totem::Wire::Rs485::Master rs485master{core.taskRegistry};
Totem::Wire::Spi::Master spiMasterGpu0{core.taskRegistry};
Totem::Wire::Spi::Master spiMasterGpu1{core.taskRegistry};
Totem::Wire::Spi::Master spiMasterLowSpeed{core.taskRegistry};
Totem::Clock::Clock clockMaster{Totem::Clock::Clock::Role::Master};
Totem::Wifi::Wifi wifi;
PubSubNetworkMasterSetup<Totem::Wire::Spi::Master, Totem::Wire::Spi::Master,
                         Totem::Wire::Rs485::Master>
    pubSubNetwork{core.taskRegistry, spiMasterLowSpeed, spiMasterGpu0,
                  spiMasterGpu1, rs485master};
Totem::PubSubBackend::Transports::UdpTransport pubSubUdpTransport{
    Totem::PubSubBackend::Transports::UdpTransportDependencies{
        .base = PubSubNetwork::makeBaseDeps(
            pubSubNetwork.node(),
            static_cast<uint8_t>(
                Totem::Data::PubSub::PubSubData<
                    Totem::Data::NodeName::Master>::Transport::UDP),
            "PubSub-UDP"),
        .taskRegistry = &core.taskRegistry,
        .networkReadyOwner = &wifi,
        .networkReady = pubSubUdpNetworkReady,
    },
};
platform::Gpio ledLevelShifterOutputEnable;
LedPresentStrobeOutput ledPresentStrobeOutput;

void setup() {
    ABORT_IF_ERR_BEGIN(core.beginStatusLedEarly(statusLedConfig));
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();
    _log_i("Core setup complete");

    ClockService::set(clockMaster);

    _log_d("Current clock time: %uus", clockMaster.nowUs());

    ABORT_IF_ERR_BEGIN(wifi.begin(wifiConfig));
    ABORT_IF_ERR(Totem::Wifi::Commands::registerCommands(wifi),
                 "Failed to register WiFi commands");
    ABORT_IF_ERR(Totem::Network::Commands::registerCommands(),
                 "Failed to register network diagnostic commands");

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

    pubSubNetwork.setup();
    if constexpr (Totem::StaticConfig::PubSubUdp::enabled) {
        _log_i("Starting UDP PubSub transport");
        ABORT_IF_ERR_BEGIN(pubSubUdpTransport.begin());
        _log_i("UDP PubSub transport began; registering with PubSub node");
        ABORT_IF_UNEXPECTED(udpHandle,
                            pubSubNetwork.node().registerTransport(
                                pubSubUdpTransport),
                            "Failed to register UDP PubSub transport");
        _log_i("UDP PubSub transport registered with PubSub node");
        (void)udpHandle;
    }

    ABORT_IF_ERR(MasterOrchestration::begin(),
                 "Failed to begin master orchestration");

    ABORT_IF_ERR(
        ledLevelShifterOutputEnable.initOutput(ledLevelShifterOutputEnablePin),
        "Failed to initialize LED level shifter output enable GPIO");
    ABORT_IF_ERR_BEGIN(ledPresentStrobeOutput.begin(ledPresentStrobeOutputPin));

    _log_i("Setup complete");
    ABORT_IF_ERR(StatusLedService::setTargetsReady(),
                 "Failed to set status LED targets-ready state");
}

extern "C" {
void app_main(void);
}

void app_main() {
    setup();

    ABORT_IF_ERR(
        ledLevelShifterOutputEnable.setLevel(ledLevelShifterOutputEnabledLevel),
        "Failed to enable LED level shifter output");
    ABORT_IF_ERR(MasterLedBringup::begin(::platform::get_time()),
                 "Failed to begin master LED bringup orchestration");

    for (;;) {
        const auto nowMs = ::platform::get_time();
        (void)core.work(nowMs);
        (void)MasterLedBringup::work(nowMs);
        (void)MasterOrchestration::work(
            nowMs, MasterLedBringup::normalOperationAllowed(nowMs));

        ::platform::delay(::platform::ms_to_ticks(1));
    }
}
