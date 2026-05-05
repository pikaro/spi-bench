#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <span>

namespace Totem::LedDisplay::detail {

template <typename Owner> struct Commands {
    static ReturnCode handle_ledwave(CommandDesc::ParsedArgs /*unused*/,
                                     void *ctx) {
        auto *display = static_cast<Owner *>(ctx);
        FAIL_IF_NULL(display, ERR(CoreError, InvalidArgument),
                     "LedDisplay command context is null");
        return display->playDefaultWave();
    }

    static inline CommandDesc ledwaveCmd = {
        .needsContext = true,
        .name = "ledwave",
        .description = "Play the default LED center wave animation",
        .args = {},
        .handler = handle_ledwave,
        .subcommands = {},
    };

    static constexpr std::span<CommandDesc *> commands() {
        static auto commands = std::to_array<CommandDesc *>({
            &ledwaveCmd,
        });
        return commands;
    }
};

} // namespace Totem::LedDisplay::detail
