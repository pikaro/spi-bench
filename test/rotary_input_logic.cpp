#include "Data/DialEvent.hpp"
#include "Data/MenuEvent.hpp"
#include "RotaryEncoder/Behavior/ButtonMenu.hpp"
#include "RotaryEncoder/Behavior/Dial.hpp"
#include "StaticConfig/PubSubEventProducer.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>

namespace {

using Totem::Data::MenuItem;
using Totem::RotaryEncoder::Direction;
using Totem::RotaryEncoder::PositionConfig;
using Totem::RotaryEncoder::Behavior::ButtonMenu;
using Totem::RotaryEncoder::Behavior::ButtonMenuEvent;
using Totem::RotaryEncoder::Behavior::ButtonMenuEventType;
using Totem::RotaryEncoder::Behavior::Dial;
using Totem::RotaryEncoder::Behavior::DialConfig;
using Totem::RotaryEncoder::Behavior::DialEvent;

static_assert(sizeof(Totem::Data::DialEvent) <=
              PubSubEventProducerConfig::maxArgumentSize);
static_assert(sizeof(Totem::Data::MenuEvent) <=
              PubSubEventProducerConfig::maxArgumentSize);

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void testDialConfigAndScaling() {
    constexpr DialConfig config{
        .position =
            {
                .initialValue = 16,
                .minimum = 0,
                .maximum = 31,
            },
    };
    static_assert(config.validate());
    static_assert(config.valueFor(0) == 0);
    static_assert(config.valueFor(16) == 132);
    static_assert(config.valueFor(31) == 255);
    static_assert(!config.valueFor(-1).has_value());
    static_assert(!config.valueFor(32).has_value());

    constexpr DialConfig asymmetric{
        .position =
            {
                .initialValue = 0,
                .minimum = -2,
                .maximum = 3,
            },
    };
    static_assert(asymmetric.valueFor(-2) == 0);
    static_assert(asymmetric.valueFor(-1) == 51);
    static_assert(asymmetric.valueFor(0) == 102);
    static_assert(asymmetric.valueFor(1) == 153);
    static_assert(asymmetric.valueFor(2) == 204);
    static_assert(asymmetric.valueFor(3) == 255);

    constexpr DialConfig fullSignedRange{
        .position =
            {
                .initialValue = 0,
                .minimum = std::numeric_limits<int32_t>::min(),
                .maximum = std::numeric_limits<int32_t>::max(),
            },
    };
    static_assert(fullSignedRange.validate());
    static_assert(
        fullSignedRange.valueFor(std::numeric_limits<int32_t>::min()) == 0);
    static_assert(fullSignedRange.valueFor(0) == 128);
    static_assert(
        fullSignedRange.valueFor(std::numeric_limits<int32_t>::max()) == 255);

    constexpr DialConfig missingMinimum{
        .position =
            {
                .initialValue = 0,
                .minimum = std::nullopt,
                .maximum = 1,
            },
    };
    constexpr DialConfig equalBounds{
        .position =
            {
                .initialValue = 1,
                .minimum = 1,
                .maximum = 1,
            },
    };
    constexpr DialConfig reversedBounds{
        .position =
            {
                .initialValue = 1,
                .minimum = 2,
                .maximum = 0,
            },
    };
    static_assert(!missingMinimum.validate());
    static_assert(!equalBounds.validate());
    static_assert(!reversedBounds.validate());
}

void testDialMovementAndBounds() {
    constexpr DialConfig config{
        .position =
            {
                .initialValue = 16,
                .minimum = 0,
                .maximum = 31,
            },
    };
    std::array<DialEvent, 64> events{};
    size_t eventCount = 0;
    Dial dial{[&events, &eventCount](DialEvent event) {
                  events[eventCount++] = event;
              },
              config};

    expect(dial.position() == 16, "dial must start at its configured position");
    expect(dial.value() == 132, "dial initial position must normalize to 132");

    dial.onRotation(Direction::Clockwise);
    expect(eventCount == 1, "accepted clockwise movement must emit");
    expect(events[0].direction == Direction::Clockwise,
           "dial event must retain clockwise direction");
    expect(events[0].position == 17,
           "dial event must carry the accepted clockwise position");
    expect(events[0].value == 140,
           "dial event must carry its normalized clockwise value");

    dial.onRotation(Direction::Counterclockwise);
    expect(eventCount == 2, "accepted counterclockwise movement must emit");
    expect(events[1].position == 16,
           "counterclockwise movement must restore the prior position");
    expect(events[1].value == 132,
           "counterclockwise event must carry the restored value");

    for (size_t i = 0; i < 16; ++i) {
        dial.onRotation(Direction::Counterclockwise);
    }
    expect(dial.position() == 0, "dial must reach its configured minimum");
    const auto eventsAtMinimum = eventCount;
    dial.onRotation(Direction::Counterclockwise);
    expect(eventCount == eventsAtMinimum,
           "movement rejected at the minimum must not emit");

    for (size_t i = 0; i < 31; ++i) {
        dial.onRotation(Direction::Clockwise);
    }
    expect(dial.position() == 31, "dial must reach its configured maximum");
    expect(dial.value() == 255, "dial maximum must normalize to 255");
    const auto eventsAtMaximum = eventCount;
    dial.onRotation(Direction::Clockwise);
    expect(eventCount == eventsAtMaximum,
           "movement rejected at the maximum must not emit");
}

void testButtonMenuRoutesAbsoluteSnapshots() {
    constexpr DialConfig dialConfig{
        .position =
            {
                .initialValue = 16,
                .minimum = 0,
                .maximum = 31,
            },
    };
    constexpr PositionConfig menuConfig{
        .initialValue = Totem::Data::mainMenuInitialPosition,
        .minimum = Totem::Data::mainMenuMinimumPosition,
        .maximum = Totem::Data::mainMenuMaximumPosition,
    };
    static_assert(menuConfig.validate());

    std::array<DialEvent, 8> dialEvents{};
    size_t dialEventCount = 0;
    Dial dial{[&dialEvents, &dialEventCount](DialEvent event) {
                  dialEvents[dialEventCount++] = event;
              },
              dialConfig};

    std::array<ButtonMenuEvent, 32> menuEvents{};
    size_t menuEventCount = 0;
    ButtonMenu menu{
        [&dial](Direction direction) { dial.onRotation(direction); },
        [&menuEvents, &menuEventCount](ButtonMenuEvent event) {
            menuEvents[menuEventCount++] = event;
        },
        menuConfig};

    menu.onRotation(Direction::Clockwise);
    expect(dialEventCount == 1 && dial.position() == 17,
           "ordinary rotation must reach the dial behavior");

    menu.onButton(Totem::Button::Event::Pressed);
    expect(menuEventCount == 1,
           "pressing the rotary switch must show the menu");
    expect(menuEvents[0].event == ButtonMenuEventType::Shown &&
               menuEvents[0].position == 0,
           "shown event must carry the configured initial position");

    menu.onRotation(Direction::Counterclockwise);
    menu.onRotation(Direction::Counterclockwise);
    expect(menuEventCount == 3,
           "accepted held rotations must emit menu movement snapshots");
    expect(menuEvents[1].event == ButtonMenuEventType::MovedCounterclockwise &&
               menuEvents[1].position == -1,
           "first held decrement must report absolute position -1");
    expect(menuEvents[2].event == ButtonMenuEventType::MovedCounterclockwise &&
               menuEvents[2].position == -2,
           "second held decrement must report absolute position -2");
    expect(dial.position() == 17 && dialEventCount == 1,
           "held menu rotation must not change the ordinary dial");

    menu.onButton(Totem::Button::Event::Released);
    expect(menuEventCount == 4,
           "releasing the rotary switch must select the menu position");
    expect(menuEvents[3].event == ButtonMenuEventType::Selected &&
               menuEvents[3].position == -2,
           "selection must carry the final absolute menu position");
    expect(Totem::Data::mainMenuItemAt(menuEvents[3].position) ==
               MenuItem::None,
           "selecting an empty menu position must remain a None item");

    menu.onRotation(Direction::Counterclockwise);
    expect(dialEventCount == 2 && dial.position() == 16,
           "ordinary routing must resume after menu selection");

    menu.onButton(Totem::Button::Event::Pressed);
    const auto eventsBeforeBound = menuEventCount;
    for (size_t i = 0; i < 20; ++i) {
        menu.onRotation(Direction::Counterclockwise);
    }
    expect(menuEventCount == eventsBeforeBound + 3,
           "menu must emit only accepted movement through its lower bound");
    expect(menuEvents[menuEventCount - 1].position == -3,
           "menu movement must stop at configured position -3");

    menu.onButton(Totem::Button::Event::Released);
    menu.onButton(Totem::Button::Event::Pressed);
    const auto eventsBeforeUpperBound = menuEventCount;
    for (size_t i = 0; i < 20; ++i) {
        menu.onRotation(Direction::Clockwise);
    }
    expect(menuEventCount == eventsBeforeUpperBound + 4,
           "menu must emit only accepted movement through its upper bound");
    expect(menuEvents[menuEventCount - 1].position == 4,
           "menu movement must stop at configured position 4");
}

void testMainMenuMapping() {
    using Totem::Data::mainMenuItemAt;
    expect(mainMenuItemAt(-3) == MenuItem::Reset,
           "menu position -3 must select Reset");
    expect(mainMenuItemAt(-2) == MenuItem::None,
           "menu position -2 must remain empty");
    expect(mainMenuItemAt(-1) == MenuItem::Next,
           "menu position -1 must select Next");
    expect(mainMenuItemAt(0) == MenuItem::Toggle,
           "menu position 0 must select Toggle");
    expect(mainMenuItemAt(1) == MenuItem::Calibrate,
           "menu position 1 must select Calibrate");
    expect(mainMenuItemAt(2) == MenuItem::Debug,
           "menu position 2 must select Debug");
    expect(mainMenuItemAt(3) == MenuItem::Battery,
           "menu position 3 must select Battery");
    expect(mainMenuItemAt(4) == MenuItem::None,
           "menu position 4 must remain empty");
    expect(mainMenuItemAt(5) == MenuItem::None,
           "out-of-range menu positions must map to None");
}

} // namespace

int main() {
    testDialConfigAndScaling();
    testDialMovementAndBounds();
    testButtonMenuRoutesAbsoluteSnapshots();
    testMainMenuMapping();

    if (failures != 0) {
        std::cerr << failures << " rotary input test(s) failed\n";
        return 1;
    }
    std::cout << "rotary input logic tests passed\n";
    return 0;
}
