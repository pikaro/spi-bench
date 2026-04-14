#pragma once

#include "Macros/Facade.hh"
#include "Types/Error.hh"
#include <cstddef>
#include <span>
#include <string_view>

struct CommandDesc {
    using Token = std::string_view;
    using Tokens = std::span<const Token>;
    using Handler = ReturnCode (*)(Tokens, void *);

    bool needsContext = false;
    void *ctx = nullptr;
    const char *name;
    const char *description;
    Tokens args;
    size_t minArgs;
    Handler handler;

    std::span<const CommandDesc> subcommands;

    [[nodiscard]] ReturnCode validate() const {
        FAIL_IF(needsContext && ctx == nullptr, ERR(InvalidArgument),
                "Command with context must have non-null context pointer");
        FAIL_IF_NULL(handler, ERR(InvalidArgument),
                     "Command handler cannot be null");
        FAIL_IF_NULL(name, ERR(InvalidArgument), "Command name cannot be null");
        FAIL_IF_NULL(description, ERR(InvalidArgument),
                     "Command description cannot be null");
        for (const auto &subcommand : subcommands) {
            FAIL_IF_ERR_FWD(subcommand.validate(),
                            "Invalid subcommand for command %s: %s", name,
                            subcommand.name);
        }
        return OK();
    }
};
