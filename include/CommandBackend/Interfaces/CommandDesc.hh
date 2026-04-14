#pragma once

#include "Macros/Facade.hh"
#include "StaticConfig/Command.hh"
#include "Types/Error.hh"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Totem::CommandBackend::detail {
template <typename T> constexpr const void *arg_type_tag();
}

struct CommandDesc {
    using Token = std::string_view;
    using Tokens = std::span<const Token>;

    struct ParseResult {
        bool ok;
    };

    using ParseFn = ParseResult (*)(Token input, void *out);

    struct Argument {
        Token name;
        bool optional;
        const void *type;
        ParseFn parse;
    };

    [[nodiscard]] ReturnCode validate() const {
        FAIL_IF(needsContext && ctx == nullptr, ERR(InvalidArgument),
                "Command with context must have non-null context pointer");
        FAIL_IF_NULL(handler, ERR(InvalidArgument),
                     "Command handler cannot be null");
        FAIL_IF_NULL(description, ERR(InvalidArgument),
                     "Command description cannot be null");
        for (const auto &subcommand : subcommands) {
            FAIL_IF_ERR_FWD(subcommand.validate(),
                            "Invalid subcommand for command %s: %s", name,
                            subcommand.name);
        }
        return OK();
    }

    enum class ArgError : uint8_t { Missing, WrongType, BadValue };

    template <typename T> struct ArgResult {
        bool ok;
        T value{};
        ArgError error{};
    };

    struct ParsedArgs {
        const CommandDesc &desc;
        CommandDesc::Tokens tokens;

        [[nodiscard]] size_t size() const { return tokens.size(); }

        template <typename T> ArgResult<T> get(size_t index) const {
            if (index >= desc.args.size()) {
                return {false, {}, ArgError::Missing};
            }

            const auto &meta = desc.args[index];

            if (meta.name.empty()) {
                return {false, {}, ArgError::Missing};
            }

            if (meta.type != Totem::CommandBackend::detail::arg_type_tag<T>()) {
                return {false, {}, ArgError::WrongType};
            }

            if (index >= tokens.size()) {
                if (meta.optional) {
                    return {false, {}, ArgError::Missing};
                }
                return {false, {}, ArgError::Missing};
            }

            T value{};
            if (!meta.parse(tokens[index], &value).ok) {
                return {false, {}, ArgError::BadValue};
            }

            return {true, value, {}};
        }
    };

    using Handler = ReturnCode (*)(ParsedArgs, void *);

    bool needsContext = false;
    void *ctx = nullptr;
    Token name;
    const char *description;
    std::array<Argument, CommandConfig::maxTokens> args;
    Handler handler;

    std::span<const CommandDesc> subcommands;
};
