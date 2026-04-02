#pragma once

#include "Macros/Facade.hh"
#include "Types/Error.hh"
#include <cstddef>
#include <span>
#include <string_view>

struct CommandDesc {
    using Token = std::string_view;
    using Tokens = std::span<const Token>;
    using Handler = ReturnCode (*)(Tokens);

    const char *name;
    const char *description;
    Tokens args;
    size_t minArgs;
    Handler handler;

    std::span<const CommandDesc> subcommands;

    [[nodiscard]] ReturnCode validate() const {
        if (name == nullptr) {
            _log_e("Command name cannot be null");
            return ERR(InvalidArgument);
        }
        if (description == nullptr) {
            _log_e("Command description cannot be null");
            return ERR(InvalidArgument);
        }
        for (const auto &subcommand : subcommands) {
            if (!subcommand.validate()) {
                _log_e("Subcommand of command %s failed validation", name);
                return ERR(InvalidArgument);
            }
        }
        return OK();
    }

    using DefaultError = CoreError;
};
