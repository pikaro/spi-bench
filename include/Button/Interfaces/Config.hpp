#pragma once

#include "DigitalInput/Interfaces/Config.hpp"

namespace Totem::Button {

struct Config {
    DigitalInput::Config input{};
    bool activeLow = true;
    bool notifyPressed = true;
    bool notifyReleased = true;

    [[nodiscard]] constexpr bool validate() const {
        return input.validate() && (notifyPressed || notifyReleased);
    }
};

} // namespace Totem::Button
