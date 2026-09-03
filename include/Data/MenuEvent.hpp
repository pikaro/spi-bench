#pragma once

#include "Data/Peripherals.hpp"
#include "Macros/internal/Markers.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Totem::Data {

enum class MenuEventType : uint8_t {
    Shown,
    MovedClockwise,
    MovedCounterclockwise,
    Selected,
};

enum class MenuItem : int8_t {
    None = -128,
    Reset = -3,
    Next = -1,
    Toggle = 0,
    Calibrate = 1,
    Debug = 2,
    Battery = 3,
};

inline constexpr int32_t mainMenuInitialPosition = 0;
inline constexpr int32_t mainMenuMinimumPosition = -3;
inline constexpr int32_t mainMenuMaximumPosition = 4;
inline constexpr size_t mainMenuPositionCount =
    static_cast<size_t>(mainMenuMaximumPosition - mainMenuMinimumPosition + 1);

inline constexpr std::array<MenuItem, mainMenuPositionCount> mainMenuItems{
    MenuItem::Reset,     // -3
    MenuItem::None,      // -2
    MenuItem::Next,      // -1
    MenuItem::Toggle,    //  0
    MenuItem::Calibrate, //  1
    MenuItem::Debug,     //  2
    MenuItem::Battery,   //  3
    MenuItem::None,      //  4
};

[[nodiscard]] constexpr MenuItem mainMenuItemAt(int32_t position) {
    if (position < mainMenuMinimumPosition ||
        position > mainMenuMaximumPosition) {
        return MenuItem::None;
    }
    return mainMenuItems[static_cast<size_t>(position -
                                             mainMenuMinimumPosition)];
}

struct WIRE_MSG MenuEvent {
    int32_t position;
    MenuEventType event;
    MenuItem item;
    PeripheralMenu menu;
};

static_assert(static_cast<int8_t>(MenuItem::Reset) == -3);
static_assert(static_cast<int8_t>(MenuItem::Next) == -1);
static_assert(static_cast<int8_t>(MenuItem::Toggle) == 0);
static_assert(static_cast<int8_t>(MenuItem::Calibrate) == 1);
static_assert(static_cast<int8_t>(MenuItem::Debug) == 2);
static_assert(static_cast<int8_t>(MenuItem::Battery) == 3);
static_assert(mainMenuItems.size() == 8);
static_assert(mainMenuItemAt(-3) == MenuItem::Reset);
static_assert(mainMenuItemAt(-2) == MenuItem::None);
static_assert(mainMenuItemAt(-1) == MenuItem::Next);
static_assert(mainMenuItemAt(0) == MenuItem::Toggle);
static_assert(mainMenuItemAt(1) == MenuItem::Calibrate);
static_assert(mainMenuItemAt(2) == MenuItem::Debug);
static_assert(mainMenuItemAt(3) == MenuItem::Battery);
static_assert(mainMenuItemAt(4) == MenuItem::None);
static_assert(sizeof(MenuEvent) == 8);
static_assert(std::is_trivially_copyable_v<MenuEvent>);

} // namespace Totem::Data
