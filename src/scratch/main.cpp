#include "Button/Behavior/PressClassifier.hpp"
#include "Button/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Queue/Facade.hpp"
#include "RotaryEncoder/Behavior/ButtonMenu.hpp"
#include "RotaryEncoder/Facade.hpp"
#include "Services/StatusLed.hpp"
#include "Setups/Core.hpp"
#include "Wire/I2C/Facade.hpp"
#include "config.hpp"
#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <cstdint>

namespace {

using ButtonEvent = Totem::Button::Event;
using Direction = Totem::RotaryEncoder::Direction;

struct RotationLog {
    Direction direction;
    int32_t position;
    uint32_t sequence;
};

constexpr size_t rotationLogQueueDepth = 32;
Totem::Queue::Platform::Storage<RotationLog, rotationLogQueueDepth>
    rotationLogQueueStorage{};
Totem::Queue::Handle rotationLogQueue = nullptr;
std::atomic<uint32_t> rotationSequence{0};
std::atomic<uint32_t> droppedRotationLogs{0};
uint32_t menuReportSequence = 0;
uint32_t gestureSequence = 0;

int32_t currentRotaryPosition();

void enqueueRotationLog(Direction direction) {
    const RotationLog event{
        .direction = direction,
        .position = currentRotaryPosition(),
        .sequence =
            rotationSequence.fetch_add(1, std::memory_order_relaxed) + 1,
    };
    const bool queued =
        rotationLogQueue != nullptr &&
        (::platform::in_isr()
             ? Totem::Queue::Platform::sendFromIsr(rotationLogQueue, &event)
             : Totem::Queue::Platform::send(rotationLogQueue, &event, 0).ok());
    if (!queued) {
        droppedRotationLogs.fetch_add(1, std::memory_order_relaxed);
    }
}

void logMenuPosition(int32_t position) {
    ++menuReportSequence;
    _log_i("Rotary menu report #%" PRIu32 ": position=%" PRId32,
           menuReportSequence, position);
}

void logButtonGesture(ButtonEvent event) {
    switch (event) {
    case ButtonEvent::Press:
        ++gestureSequence;
        _log_i("Gesture button #%" PRIu32 ": press", gestureSequence);
        return;
    case ButtonEvent::LongPress:
        ++gestureSequence;
        _log_i("Gesture button #%" PRIu32 ": long press", gestureSequence);
        return;
    case ButtonEvent::DoublePress:
        ++gestureSequence;
        _log_i("Gesture button #%" PRIu32 ": double press", gestureSequence);
        return;
    case ButtonEvent::Pressed:
    case ButtonEvent::Released:
        return;
    }
}

Totem::RotaryEncoder::Behavior::ButtonMenu buttonMenu{
    [](Direction /*unused*/) {}, logMenuPosition, menuPositionConfig};

Totem::RotaryEncoder::RotaryEncoder rotaryEncoder{[](Direction direction) {
    enqueueRotationLog(direction);
    buttonMenu.onRotation(direction);
}};

int32_t currentRotaryPosition() { return rotaryEncoder.position(); }

Totem::Button::Button rotarySwitch{
    [](ButtonEvent event) { buttonMenu.onButton(event); }};

Totem::Button::Behavior::PressClassifier gestureBehavior{logButtonGesture,
                                                         gestureConfig};

Totem::Button::Button gestureButton{[](ButtonEvent event) {
    gestureBehavior.onButton(event, ::platform::get_time());
}};

void workRotationLogs() {
    RotationLog event{};
    while (rotationLogQueue != nullptr &&
           Totem::Queue::Platform::receive(rotationLogQueue, &event, 0).ok()) {
        _log_i("Rotary detent #%" PRIu32 ": position=%" PRId32 ", %s",
               event.sequence, event.position,
               event.direction == Direction::Clockwise ? "clockwise"
                                                       : "counterclockwise");
    }

    const auto dropped =
        droppedRotationLogs.exchange(0, std::memory_order_relaxed);
    if (dropped != 0) {
        _log_w("Dropped %" PRIu32 " rotary increment logs", dropped);
    }
}

} // namespace

CoreSetup core{};
Totem::Wire::I2C::Master i2cMaster{};
Totem::Wire::I2C::Ina2xx ina226{i2cMaster,
                                Totem::Wire::I2C::Ina2xxModel::Ina226};

void setup() {
    ABORT_IF_ERR_BEGIN(core.beginStatusLedEarly(statusLedConfig));
    ::platform::delay(::platform::ms_to_ticks(3000));

    core.setup();
    _log_i("Core setup complete");

    ABORT_IF_ERR_BEGIN(i2cMaster.begin(i2cMasterConfig));
    ABORT_IF_ERR_BEGIN(ina226.begin(ina226Config));
    _log_i("INA226 detected at I2C address 0x%02X",
           static_cast<unsigned>(ina226Config.device.address));

    ABORT_IF_UNEXPECTED(queue,
                        Totem::Queue::Platform::create(rotationLogQueueStorage),
                        "Failed to create the rotary log queue");
    rotationLogQueue = queue;

    ABORT_IF_ERR_BEGIN(rotaryEncoder.begin(rotaryEncoderConfig));
    ABORT_IF_ERR_BEGIN(rotarySwitch.begin(rotarySwitchConfig));
    ABORT_IF_ERR_BEGIN(gestureButton.begin(gestureButtonConfig));

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
        REPORT_IF_ERR(ina226.work(nowMs), "INA226 work failed");
        REPORT_IF_ERR(rotaryEncoder.work(nowMs), "Rotary encoder work failed");
        REPORT_IF_ERR(rotarySwitch.work(nowMs), "Rotary switch work failed");
        REPORT_IF_ERR(gestureButton.work(nowMs), "Gesture button work failed");
        gestureBehavior.work(nowMs);
        workRotationLogs();
        ::platform::delay(::platform::ms_to_ticks(1));
    }
}
