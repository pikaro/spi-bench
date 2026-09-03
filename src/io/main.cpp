#include "Bluetooth/Facade.hpp"
#include "Button/Facade.hpp"
#include "Clock/Facade.hpp"
#include "Data/ButtonEvent.hpp"
#include "Data/DialEvent.hpp"
#include "Data/MenuEvent.hpp"
#include "Data/Peripherals.hpp"
#include "LedPwm/Facade.hpp"
#include "LedPwm/Interfaces/CommandEvent.hpp"
#include "LedPwm/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubEventProducer/Facade.hpp"
#include "RotaryEncoder/Behavior/ButtonMenu.hpp"
#include "RotaryEncoder/Behavior/Dial.hpp"
#include "RotaryEncoder/Facade.hpp"
#include "Services/PubSub.hpp"
#include "Setups/ClockSync.hpp"
#include "Setups/Core.hpp"
#include "Setups/PubSubNetwork.hpp"
#include "Types/Error.hpp"
#include "Wheel/Facade.hpp"
#include "Wire/Rs485/Facade.hpp"
#include "config.hpp"

CoreSetup core{};

Totem::Wire::Rs485::Slave rs485Slave{core.taskRegistry};
Totem::Clock::Clock clockSlave{Totem::Clock::Clock::Role::Slave};
ClockSyncSetup<Totem::Wire::Rs485::Slave> clockSync{clockSlave, rs485Slave};
PubSubNetworkRs485EdgeSetup<Totem::Wire::Rs485::Slave> pubSubNetwork{
    core.taskRegistry, rs485Slave};

Totem::PubSubEventProducer::Producer eventProducer{core.taskRegistry};

static auto makeButtonEventCallback(PeripheralButton button) {
    return eventProducer.makeCallback<Totem::Button::Event>(
        PubSubService::Topic::Button, [button](Totem::Button::Event event) {
            return Totem::Data::ButtonEvent{
                .event = event,
                .button = button,
            };
        });
}

namespace {

using Totem::RotaryEncoder::Behavior::ButtonMenuEvent;
using Totem::RotaryEncoder::Behavior::ButtonMenuEventType;

[[nodiscard]] constexpr Totem::Data::MenuEventType
toMenuEventType(ButtonMenuEventType event) {
    switch (event) {
    case ButtonMenuEventType::Shown:
        return Totem::Data::MenuEventType::Shown;
    case ButtonMenuEventType::MovedClockwise:
        return Totem::Data::MenuEventType::MovedClockwise;
    case ButtonMenuEventType::MovedCounterclockwise:
        return Totem::Data::MenuEventType::MovedCounterclockwise;
    case ButtonMenuEventType::Selected:
        return Totem::Data::MenuEventType::Selected;
    }
    return Totem::Data::MenuEventType::Shown;
}

auto dialEventPublisher = eventProducer.makeCallback<Totem::Data::DialEvent>(
    PubSubService::Topic::Dial,
    [](Totem::Data::DialEvent event) { return event; });

void publishBrightnessDialEvent(
    Totem::RotaryEncoder::Behavior::DialEvent event) {
    (void)dialEventPublisher(Totem::Data::DialEvent{
        .position = event.position,
        .event = event.direction,
        .dial = PeripheralDial::Main,
        .value = event.value,
    });
}

Totem::RotaryEncoder::Behavior::Dial brightnessDial{publishBrightnessDialEvent,
                                                    brightnessDialConfig};

auto menuEventPublisher = eventProducer.makeCallback<Totem::Data::MenuEvent>(
    PubSubService::Topic::Menu,
    [](Totem::Data::MenuEvent event) { return event; });

void publishMainMenuEvent(ButtonMenuEvent event) {
    (void)menuEventPublisher(Totem::Data::MenuEvent{
        .position = event.position,
        .event = toMenuEventType(event.event),
        .item = Totem::Data::mainMenuItemAt(event.position),
        .menu = PeripheralMenu::Main,
    });
}

Totem::RotaryEncoder::Behavior::ButtonMenu mainMenu{
    [](Totem::RotaryEncoder::Direction direction) {
        brightnessDial.onRotation(direction);
    },
    publishMainMenuEvent, mainMenuPositionConfig};

Totem::RotaryEncoder::RotaryEncoder rotaryEncoder{
    [](Totem::RotaryEncoder::Direction direction) {
        mainMenu.onRotation(direction);
    }};

Totem::Button::Button rotarySwitch{
    [](Totem::Button::Event event) { mainMenu.onButton(event); }};

} // namespace

Totem::Button::Button bellButton{
    makeButtonEventCallback(PeripheralButton::Bell)};
Totem::Button::Button calibrationButton{
    makeButtonEventCallback(PeripheralButton::Calibration)};
Totem::LedPwm::LedPwm ledPwm{core.taskRegistry};
Totem::Wheel::BleWheel wheel{};
Totem::Bluetooth::Central bluetooth{core.taskRegistry};

static ReturnCode
ledPwmCommandCallback(void * /*unused*/,
                      const Totem::PubSubBackend::Envelope &envelope);
static ReturnCode
runLedPwmCommandEvent(const Totem::LedPwm::CommandEvent &event,
                      Totem::LedPwm::LedContext ledCtx);

void setup() {
    ABORT_IF_ERR_BEGIN(core.beginStatusLedEarly(statusLedConfig));
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();
    _log_i("Core setup complete");

    ABORT_IF_ERR_BEGIN(rs485Slave.begin(rs485SlaveConfig));

    ABORT_IF_ERR_BEGIN(ledPwm.begin(ledPwmConfig));
    pubSubNetwork.setup();
    ABORT_IF_UNEXPECTED(
        _,
        PubSubService::get().subscribe(
            "io-led",
            {.subscriber = nullptr, .callback = ledPwmCommandCallback},
            PubSubService::Topic::LedPwm),
        "Failed to subscribe to LED PWM command events");
    ABORT_IF_ERR_BEGIN(wheel.begin(wheelConfig));
    ABORT_IF_ERR_BEGIN(bluetooth.begin(makeBluetoothConfig(wheel)));
    ABORT_IF_ERR_BEGIN(eventProducer.begin());
    ABORT_IF_ERR_BEGIN(bellButton.begin(bellButtonConfig));
    ABORT_IF_ERR_BEGIN(calibrationButton.begin(calibrationButtonConfig));
    ABORT_IF_ERR_BEGIN(rotaryEncoder.begin(rotaryEncoderConfig));
    ABORT_IF_ERR_BEGIN(rotarySwitch.begin(rotarySwitchConfig));

    _log_i("Setup complete");
    ABORT_IF_ERR(StatusLedService::setTargetsReady(),
                 "Failed to set status LED targets-ready state");
}

extern "C" {
void app_main(void);
}

static ReturnCode
ledPwmCommandCallback(void * /*unused*/,
                      const Totem::PubSubBackend::Envelope &envelope) {
    FAIL_IF_UNEXPECTED_FWD(
        event, envelope.getPayloadAs<Totem::LedPwm::CommandEvent>(),
        "Failed to decode LED PWM command event from envelope payload");
    FAIL_IF_UNEXPECTED_FWD(ledCtx, ledPwm.getLedContext(event.led),
                           "Failed to get LED context for command event");
    return runLedPwmCommandEvent(event, ledCtx);
}

static ReturnCode
runLedPwmCommandEvent(const Totem::LedPwm::CommandEvent &event,
                      Totem::LedPwm::LedContext ledCtx) {
    FAIL_IF_NOT(event.validate(), ERR(InvalidArgument),
                "Invalid LED PWM command event");

    switch (event.type) {
    case Totem::LedPwm::CommandEventType::SetBrightness:
        return Totem::LedPwm::LedCommand::setBrightness(event.brightness)
            .run(ledCtx);
    case Totem::LedPwm::CommandEventType::StartPulse:
        return Totem::LedPwm::LedCommand::pulse(event.pulse).run(ledCtx);
    case Totem::LedPwm::CommandEventType::StartGlitter:
        return Totem::LedPwm::LedCommand::startAnimation(
                   Totem::LedPwm::Animation{event.glitter})
            .run(ledCtx);
    case Totem::LedPwm::CommandEventType::ClearAnimations:
        return Totem::LedPwm::LedCommand::clearAnimations().run(ledCtx);
    case Totem::LedPwm::CommandEventType::None:
    default:
        return ERR(InvalidArgument);
    }
}

void app_main() {
    setup();

    for (;;) {
        const auto nowMs = ::platform::get_time();
        REPORT_IF_ERR(core.work(nowMs), "Core work failed");
        REPORT_IF_ERR(clockSync.work(nowMs), "Clock sync work failed");
        REPORT_IF_ERR(bellButton.work(nowMs), "Bell button work failed");
        REPORT_IF_ERR(calibrationButton.work(nowMs),
                      "Calibration button work failed");
        REPORT_IF_ERR(rotaryEncoder.work(nowMs), "Rotary encoder work failed");
        REPORT_IF_ERR(rotarySwitch.work(nowMs), "Rotary switch work failed");

        ::platform::delay(::platform::ms_to_ticks(1));
    }
}
