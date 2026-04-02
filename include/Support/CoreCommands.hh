#pragma once

#include "Macros/Facade.hh"
#include "Support/Commands.hh"
#include "Types/Command.hh"
#include "Types/Error.hh"

inline static CommandDesc helloCmd = {
    .name = "hello",
    .description = "Prints Hello, World!",
    .args = {},
    .minArgs = 0,
    .handler = [](CommandDesc::Tokens) -> ReturnCode {
        _log_i("Hello, World!");
        return OK(CoreError);
    },
    .subcommands = {},
};

inline static ReturnCode register_core_commands() {
    auto &reg = Commands::registrar();

    FAIL_IF_ERR_FWD(reg.registerCommand(helloCmd),
                    "Failed to register hello command");

    return OK(CoreError);
}
