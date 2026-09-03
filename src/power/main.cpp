#include "BatteryMonitor/Commands/Adapter.hpp"
#include "BatteryMonitor/Facade.hpp"
#include "BatteryMonitor/Interfaces/Wire.hpp"
#include "Clock/Facade.hpp"
#include "Data/Nodes.hpp"
#include "Data/PubSub.hpp"
#include "Macros/Facade.hpp"
#include "Network/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "PubSubBackend/Transports/UdpTransport.hpp"
#include "Services/PubSub.hpp"
#include "Services/StatusLed.hpp"
#include "Setups/ClockSync.hpp"
#include "Setups/Core.hpp"
#include "Setups/PubSubNetwork.hpp"
#include "StaticConfig/PubSubUdp.hpp"
#include "Types/Error.hpp"
#include "Wifi/Facade.hpp"
#include "Wire/I2C/Facade.hpp"
#include "Wire/Spi/Facade.hpp"
#include "config.hpp"
#include <algorithm>
#include <cstdint>

CoreSetup core{};
Totem::Clock::Clock clockSlave{Totem::Clock::Clock::Role::Slave};
Totem::BatteryMonitor::BatteryMonitor batteryMonitor{};
Totem::BatteryMonitor::Commands::Adapter batteryCommands{};
Totem::Wire::I2C::Master i2cMaster{};
Totem::Wire::I2C::Ina2xx ina24v{i2cMaster,
                                Totem::Wire::I2C::Ina2xxModel::Ina226};
Totem::Wire::I2C::Ina2xx ina5v{i2cMaster,
                               Totem::Wire::I2C::Ina2xxModel::Ina226};
Totem::Wire::Spi::Slave spiSlave{core.taskRegistry};
ClockSyncSetup<Totem::Wire::Spi::Slave> clockSync{clockSlave, spiSlave};
PubSubNetworkSpiEdgeSetup<Totem::Wire::Spi::Slave, Totem::Data::NodeName::Power>
    pubSubNetwork{core.taskRegistry, spiSlave};

namespace {

uint32_t lastBatteryStatusPublishMs = 0;

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

void observeBatterySample(const Totem::Wire::I2C::Ina2xxSample &sample) {
    const Totem::BatteryMonitor::BatteryMeasurement measurement{
        .capturedAtMs = sample.capturedAtMs,
        .voltageMillivolts = sample.busMillivolts,
        .currentMicroamps = sample.currentMicroamps,
        .powerMilliwatts = sample.powerMilliwatts,
    };
    auto observation = batteryMonitor.observe(measurement);
    if (!observation) {
        REPORT_IF_ERR(observation.error(), "Battery sample observation failed");
    }
}

ReturnCode publishBatteryStatus(uint32_t nowMs) {
    const auto elapsed = nowMs - lastBatteryStatusPublishMs;
    if (lastBatteryStatusPublishMs != 0 &&
        elapsed < batteryStatusPublishIntervalMs) {
        return OK();
    }
    lastBatteryStatusPublishMs = nowMs;

    FAIL_IF_UNEXPECTED_FWD(status, batteryMonitor.status(),
                           "Failed to snapshot battery status for PubSub");
    const auto stateOfCharge = static_cast<uint16_t>(
        std::min<uint32_t>(status.stateOfChargePartsPerThousand, 1'000U));
    return PubSubService::publish(
        PubSubService::Topic::Power,
        Totem::BatteryMonitor::BatteryStatusEvent{
            .stateOfChargePartsPerThousand = stateOfCharge,
            .sourceState = status.sourceState,
            .measurementFreshness = status.measurementFreshness,
            .confidence = status.confidence,
        });
}

} // namespace

Totem::Wifi::Wifi wifi;
Totem::PubSubBackend::Transports::UdpTransport pubSubUdpTransport{
    Totem::PubSubBackend::Transports::UdpTransportDependencies{
        .base = PubSubNetwork::makeBaseDeps(
            pubSubNetwork.node(),
            static_cast<uint8_t>(Totem::Data::PubSub::PubSubData<
                                 Totem::Data::NodeName::Power>::Transport::UDP),
            "PubSub-UDP"),
        .taskRegistry = &core.taskRegistry,
        .networkReadyOwner = &wifi,
        .networkReady = pubSubUdpNetworkReady,
    },
};

void setup() {
    ABORT_IF_ERR_BEGIN(core.beginStatusLedEarly(statusLedConfig));
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();
    _log_i("Core setup complete");

    ABORT_IF_ERR_BEGIN(batteryMonitor.begin(batteryConfig));
    ABORT_IF_ERR_BEGIN(batteryCommands.begin(batteryMonitor));
    ABORT_IF_ERR_BEGIN(ina24v.setSampleCallback(observeBatterySample));

    ABORT_IF_ERR_BEGIN(i2cMaster.begin(i2cMasterConfig));
    ABORT_IF_ERR_BEGIN(ina24v.begin(ina24vConfig));
    _log_i("24 V INA226 detected at I2C address 0x%02X",
           static_cast<unsigned>(ina24vConfig.device.address));
    ABORT_IF_ERR_BEGIN(ina5v.begin(ina5vConfig));
    _log_i("5 V INA226 detected at I2C address 0x%02X",
           static_cast<unsigned>(ina5vConfig.device.address));

    ABORT_IF_ERR_BEGIN(spiSlave.begin(spiSlaveConfig));
    pubSubNetwork.setup();

    ABORT_IF_ERR_BEGIN(wifi.begin(wifiConfig));
    ABORT_IF_ERR(Totem::Network::Commands::registerCommands(),
                 "Failed to register network diagnostic commands");

    if constexpr (Totem::StaticConfig::PubSubUdp::enabled) {
        _log_i("Starting UDP PubSub transport");
        ABORT_IF_ERR_BEGIN(pubSubUdpTransport.begin());
        ABORT_IF_UNEXPECTED(
            udpHandle,
            pubSubNetwork.node().registerTransport(pubSubUdpTransport),
            "Failed to register UDP PubSub transport");
        (void)udpHandle;
        _log_i("UDP PubSub transport registered with Power PubSub node");
    }

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
        REPORT_IF_ERR(ina24v.work(nowMs), "24 V INA226 work failed");
        REPORT_IF_ERR(ina5v.work(nowMs), "5 V INA226 work failed");
        REPORT_IF_ERR(batteryMonitor.work(nowMs),
                      "Battery monitor work failed");
        REPORT_IF_ERR(publishBatteryStatus(nowMs),
                      "Battery status publication failed");
        ::platform::delay(::platform::ms_to_ticks(1));
    }
}
