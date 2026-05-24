#include "Buttons/Facade.hpp"
#include "Buttons/Interfaces/Wire.hpp"
#include "Bluetooth/Facade.hpp"
#include "Clock/Facade.hpp"
#include "Data/Peripherals.hpp"
#include "LedPwm/Facade.hpp"
#include "LedPwm/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "Services/PubSub.hpp"
#include "Setups/ClockSync.hpp"
#include "Setups/Core.hpp"
#include "Setups/PubSubNetwork.hpp"
#include "Types/Error.hpp"
#include "Wheel/Facade.hpp"
#include "Wire/Rs485/Facade.hpp"
#include "config.hpp"
#include <cstdint>

CoreSetup core{};

Totem::Wire::Rs485::Slave rs485Slave{core.taskRegistry};
Totem::Clock::Clock clockSlave{Totem::Clock::Clock::Role::Slave};
ClockSyncSetup<Totem::Wire::Rs485::Slave> clockSync{clockSlave, rs485Slave};
PubSubNetworkRs485EdgeSetup<Totem::Wire::Rs485::Slave> pubSubNetwork{
    core.taskRegistry, rs485Slave};

Totem::Buttons::Buttons buttons{core.taskRegistry};
Totem::LedPwm::LedPwm ledPwm{core.taskRegistry};
Totem::Wheel::BleWheel wheel{};
Totem::Bluetooth::Central bluetooth{core.taskRegistry};

static ReturnCode
buttonsCallback(void * /*unused*/,
                const Totem::PubSubBackend::Envelope &envelope);

void setup() {
    ABORT_IF_ERR_BEGIN(core.beginStatusLedEarly(statusLedConfig));
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();
    _log_i("Core setup complete");

    ABORT_IF_ERR_BEGIN(rs485Slave.begin(rs485SlaveConfig));

    ABORT_IF_ERR_BEGIN(ledPwm.begin(ledPwmConfig));
    pubSubNetwork.setup();
    ABORT_IF_ERR_BEGIN(wheel.begin(wheelConfig));
    ABORT_IF_ERR_BEGIN(bluetooth.begin(makeBluetoothConfig(wheel)));
    ABORT_IF_UNEXPECTED(
        _,
        PubSubService::get().subscribe(
            "buttons-sub", {.subscriber = nullptr, .callback = buttonsCallback},
            PubSubService::Topic::Button),
        "Failed to subscribe to button events");
    ABORT_IF_ERR_BEGIN(buttons.begin(buttonsConfig));

    _log_i("Setup complete");
    ABORT_IF_ERR(StatusLedService::setTargetsReady(),
                 "Failed to set status LED targets-ready state");
}

extern "C" {
void app_main(void);
}

static ReturnCode
buttonsCallback(void * /*unused*/,
                const Totem::PubSubBackend::Envelope &envelope) {
    _log_i("Received button event: topic=%u payloadSize=%zu",
           static_cast<unsigned>(envelope.header.topic),
           envelope.header.payloadSize);

    FAIL_IF_UNEXPECTED_FWD(
        event, envelope.getPayloadAs<Totem::Buttons::ButtonEvent>(),
        "Failed to decode button event from envelope payload");

    if (event.type == Totem::Buttons::ButtonEventType::Pressed) {
        _log_i("Button %u pressed", event.button);
    } else {
        _log_i("Button %u released", event.button);
        return OK();
    }

    ABORT_IF_UNEXPECTED(ledCtxBulb1, ledPwm.getLedContext(PeripheralLed::Bulb1),
                        "Failed to get LED context for Bulb1");
    ABORT_IF_UNEXPECTED(ledCtxBulb2, ledPwm.getLedContext(PeripheralLed::Bulb2),
                        "Failed to get LED context for Bulb2");
    ABORT_IF_UNEXPECTED(ledCtxOnboard,
                        ledPwm.getLedContext(PeripheralLed::Onboard),
                        "Failed to get LED context for Onboard");

    auto cmd = Totem::LedPwm::LedCommand::pulse({
        .riseMs = 100,
        .holdMs = 100,
        .fallMs = 800,
        .curve = Totem::LedPwm::Curve::SmoothStep,
    });
    (void)cmd.run(ledCtxBulb1);
    (void)cmd.run(ledCtxBulb2);
    (void)cmd.run(ledCtxOnboard);

    return OK();
}

void app_main() {
    setup();

    for (;;) {
        const auto nowMs = ::platform::get_time();
        (void)core.work(nowMs);
        (void)clockSync.work(nowMs);

        ::platform::delay(::platform::ms_to_ticks(1));
    }
}
