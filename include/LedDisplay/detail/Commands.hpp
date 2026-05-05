#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Interfaces/Types.hpp"
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

    static ReturnCode handle_ledprim(CommandDesc::ParsedArgs args,
                                     void *ctx) {
        auto *display = static_cast<Owner *>(ctx);
        FAIL_IF_NULL(display, ERR(CoreError, InvalidArgument),
                     "LedDisplay command context is null");
        auto primitive = args.get<PrimitiveKind>(0);
        if (!primitive.ok &&
            primitive.error == CommandDesc::ArgError::Missing) {
            return display->playPrimitive(PrimitiveKind::Explosion);
        }
        FAIL_IF_NOT(primitive.ok, ERR(CoreError, InvalidArgument),
                    "Invalid LED primitive kind");
        return display->playPrimitive(primitive.value);
    }

    static inline CommandDesc ledwaveCmd = {
        .needsContext = true,
        .name = "ledwave",
        .description = "Play the default LED center wave animation",
        .args = {},
        .handler = handle_ledwave,
        .subcommands = {},
    };

    static inline CommandDesc ledprimCmd = {
        .needsContext = true,
        .name = "ledprim",
        .description = "Play a local LED primitive demo animation",
        .args = {Totem::CommandBackend::detail::arg<PrimitiveKind>(
            "primitive", CommandDesc::ArgRequirement::Optional)},
        .handler = handle_ledprim,
        .subcommands = {},
    };

    static constexpr std::span<CommandDesc *> commands() {
        static auto commands = std::to_array<CommandDesc *>({
            &ledwaveCmd,
            &ledprimCmd,
        });
        return commands;
    }
};

} // namespace Totem::LedDisplay::detail
