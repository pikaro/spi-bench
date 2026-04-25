#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "Macros/Facade.hpp"
#include "Services/Commands.hpp"
#include "Types/Error.hpp"

inline CommandDesc helloCmd = {
    .name = "hello",
    .description = "Prints Hello, World!",
    .args = {},
    .handler = [](CommandDesc::ParsedArgs /*unused*/, void *) -> ReturnCode {
        _log_i("Hello, World!");
        return OK(CoreError);
    },
    .subcommands = {},
};

inline ReturnCode register_core_commands() {
    auto &reg = CommandRegistrarService::get();

    FAIL_IF_UNEXPECTED_FWD(_, reg.registerCommand(helloCmd),
                           "Failed to register hello command");

    return OK(CoreError);
}
