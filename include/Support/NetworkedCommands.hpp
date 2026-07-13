#pragma once

#include "LedDisplay/Interfaces/Commands.hpp"

inline ReturnCode register_network_commands() {
    FAIL_IF_ERR_FWD(register_display_commands(),
                    "Failed to register LED display commands");
    return OK(CoreError);
}
