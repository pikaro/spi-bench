#pragma once

#include "CommandBackend/Facade.hpp"
#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "Macros/Facade.hpp"
#include "SecretStorage/Interfaces/Entry.hpp"
#include "StaticConfig/Command.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace Totem::SecretStorage::detail {

template <typename Owner> struct Commands {
    static constexpr std::string_view sealKey = "seal";
    static constexpr std::size_t minSealTokenLength = 8;

    static ReturnCode requireUnsealed(const Owner &storage) {
        FAIL_IF(storage._commandsSealed, ERR(CoreError, Forbidden),
                "Secret commands are sealed; use /secret list or /secret "
                "unseal <token>");
        return OK();
    }

    static ReturnCode handleRoot(CommandDesc::ParsedArgs /*unused*/,
                                 void *ctx) {
        auto *storage = static_cast<Owner *>(ctx);
        FAIL_IF_NULL(storage, ERR(CoreError, InvalidArgument),
                     "SecretStorage command context is null");
        FAIL_IF_ERR_FWD(requireUnsealed(*storage),
                        "Cannot use the secret root command while sealed");

        _log_i("Use /secret get|set|delete|list|seal|unseal");
        return OK();
    }

    static ReturnCode handleGet(CommandDesc::ParsedArgs args, void *ctx) {
        auto *storage = static_cast<Owner *>(ctx);
        FAIL_IF_NULL(storage, ERR(CoreError, InvalidArgument),
                     "SecretStorage command context is null");
        FAIL_IF_ERR_FWD(requireUnsealed(*storage),
                        "Cannot read secrets while sealed");

        const auto key = args.template get<std::string_view>(0);
        FAIL_IF(!key.ok, ERR(CommandError, BadArgument),
                "Missing or invalid key for /secret get");

        FAIL_IF_UNEXPECTED_FWD(valueSize, storage->size(key.value),
                               "Failed to find secret " SV_FMT,
                               SV_ARG(key.value));
        FAIL_IF(valueSize > CommandConfig::maxLineLen,
                ERR(CoreError, InvalidSize),
                "Secret " SV_FMT " is too large for console output",
                SV_ARG(key.value));

        std::array<std::byte, CommandConfig::maxLineLen> value{};
        FAIL_IF_UNEXPECTED_FWD(
            bytesRead,
            storage->get(key.value,
                         std::span<std::byte>{value}.first(valueSize)),
            "Failed to read secret " SV_FMT, SV_ARG(key.value));

        _log_i("Secret " SV_FMT " (%zu bytes): %.*s", SV_ARG(key.value),
               bytesRead, static_cast<int>(bytesRead),
               reinterpret_cast<const char *>(value.data()));
        return OK();
    }

    static ReturnCode handleSet(CommandDesc::ParsedArgs args, void *ctx) {
        auto *storage = static_cast<Owner *>(ctx);
        FAIL_IF_NULL(storage, ERR(CoreError, InvalidArgument),
                     "SecretStorage command context is null");
        FAIL_IF_ERR_FWD(requireUnsealed(*storage),
                        "Cannot store secrets while sealed");

        const auto key = args.template get<std::string_view>(0);
        const auto value = args.template get<std::string_view>(1);
        FAIL_IF(!key.ok || !value.ok, ERR(CommandError, BadArgument),
                "Missing or invalid arguments for /secret set");

        std::array<std::byte, CommandConfig::maxLineLen> bytes{};
        std::size_t bytesUsed = 0;
        for (std::size_t i = 1; i < args.tokens.size(); ++i) {
            if (i > 1) {
                FAIL_IF(bytesUsed == bytes.size(), ERR(CoreError, InvalidSize),
                        "Secret value exceeds console line capacity");
                bytes[bytesUsed++] = std::byte{' '};
            }

            const auto token = args.tokens[i];
            FAIL_IF(token.size() > bytes.size() - bytesUsed,
                    ERR(CoreError, InvalidSize),
                    "Secret value exceeds console line capacity");
            std::memcpy(bytes.data() + bytesUsed, token.data(), token.size());
            bytesUsed += token.size();
        }

        FAIL_IF_ERR_FWD(
            storage->set(key.value,
                         std::span<const std::byte>{bytes}.first(bytesUsed)),
            "Failed to store secret " SV_FMT, SV_ARG(key.value));
        _log_i("Stored secret " SV_FMT " (%zu bytes)", SV_ARG(key.value),
               bytesUsed);
        return OK();
    }

    static ReturnCode handleDelete(CommandDesc::ParsedArgs args, void *ctx) {
        auto *storage = static_cast<Owner *>(ctx);
        FAIL_IF_NULL(storage, ERR(CoreError, InvalidArgument),
                     "SecretStorage command context is null");
        FAIL_IF_ERR_FWD(requireUnsealed(*storage),
                        "Cannot delete secrets while sealed");

        const auto key = args.template get<std::string_view>(0);
        FAIL_IF(!key.ok, ERR(CommandError, BadArgument),
                "Missing or invalid key for /secret delete");

        FAIL_IF_ERR_FWD(storage->erase(key.value),
                        "Failed to delete secret " SV_FMT, SV_ARG(key.value));
        _log_i("Deleted secret " SV_FMT, SV_ARG(key.value));
        return OK();
    }

    static ReturnCode handleList(CommandDesc::ParsedArgs /*unused*/,
                                 void *ctx) {
        auto *storage = static_cast<Owner *>(ctx);
        FAIL_IF_NULL(storage, ERR(CoreError, InvalidArgument),
                     "SecretStorage command context is null");

        std::size_t count = 0;
        FAIL_IF_ERR_FWD(storage->list([&count](const Entry &entry) {
            _log_i("Secret: " SV_FMT " (%zu bytes)", SV_ARG(entry.key),
                   entry.size);
            ++count;
            return OK();
        }),
                        "Failed to list secrets");
        _log_i("Secret count: %zu", count);
        return OK();
    }

    static ReturnCode handleSeal(CommandDesc::ParsedArgs /*unused*/,
                                 void *ctx) {
        auto *storage = static_cast<Owner *>(ctx);
        FAIL_IF_NULL(storage, ERR(CoreError, InvalidArgument),
                     "SecretStorage command context is null");
        FAIL_IF_ERR_FWD(requireUnsealed(*storage),
                        "Secret commands are already sealed");

        FAIL_IF_UNEXPECTED_FWD(tokenSize, storage->size(sealKey),
                               "Cannot seal without a " SV_FMT " secret",
                               SV_ARG(sealKey));
        FAIL_IF(tokenSize < minSealTokenLength, ERR(CoreError, InvalidSize),
                "Secret " SV_FMT " must contain at least %zu characters",
                SV_ARG(sealKey), minSealTokenLength);

        storage->_commandsSealed = true;
        _log_i("Secret commands sealed");
        return OK();
    }

    static ReturnCode handleUnseal(CommandDesc::ParsedArgs args, void *ctx) {
        auto *storage = static_cast<Owner *>(ctx);
        FAIL_IF_NULL(storage, ERR(CoreError, InvalidArgument),
                     "SecretStorage command context is null");

        const auto token = args.template get<std::string_view>(0);
        FAIL_IF(!token.ok, ERR(CommandError, BadArgument),
                "Missing or invalid token for /secret unseal");

        FAIL_IF_UNEXPECTED_FWD(tokenSize, storage->size(sealKey),
                               "Cannot unseal without a " SV_FMT " secret",
                               SV_ARG(sealKey));
        FAIL_IF(tokenSize != token.value.size(), ERR(CoreError, Forbidden),
                "Invalid unseal token");

        std::array<std::byte, CommandConfig::maxLineLen> storedToken{};
        FAIL_IF(tokenSize > storedToken.size(), ERR(CoreError, InvalidSize),
                "Secret " SV_FMT " is too large for console input",
                SV_ARG(sealKey));
        FAIL_IF_UNEXPECTED_FWD(
            bytesRead,
            storage->get(sealKey,
                         std::span<std::byte>{storedToken}.first(tokenSize)),
            "Failed to read secret " SV_FMT, SV_ARG(sealKey));
        FAIL_IF(bytesRead != tokenSize, ERR(CoreError, InvalidSize),
                "Secret " SV_FMT " changed while being read", SV_ARG(sealKey));

        std::uint8_t difference = 0;
        for (std::size_t i = 0; i < tokenSize; ++i) {
            difference |= std::to_integer<std::uint8_t>(storedToken[i]) ^
                          static_cast<std::uint8_t>(token.value[i]);
        }
        FAIL_IF(difference != 0, ERR(CoreError, Forbidden),
                "Invalid unseal token");

        storage->_commandsSealed = false;
        _log_i("Secret commands unsealed");
        return OK();
    }

    static inline constinit std::array<CommandDesc, 6> subcommands{{
        {
            .name = "get",
            .description = "Read a secret",
            .args = {CommandBackend::arg<std::string_view>("key")},
            .handler = handleGet,
            .subcommands = {},
        },
        {
            .name = "set",
            .description = "Store remaining tokens as a secret",
            .args = {CommandBackend::arg<std::string_view>("key"),
                     CommandBackend::arg<std::string_view>("value")},
            .handler = handleSet,
            .subcommands = {},
        },
        {
            .name = "delete",
            .description = "Delete a secret",
            .args = {CommandBackend::arg<std::string_view>("key")},
            .handler = handleDelete,
            .subcommands = {},
        },
        {
            .name = "list",
            .description = "List secret keys and sizes",
            .args = {},
            .handler = handleList,
            .subcommands = {},
        },
        {
            .name = "seal",
            .description = "Seal secret commands",
            .args = {},
            .handler = handleSeal,
            .subcommands = {},
        },
        {
            .name = "unseal",
            .description = "Unseal secret commands",
            .args = {CommandBackend::arg<std::string_view>("token")},
            .handler = handleUnseal,
            .subcommands = {},
        },
    }};

    static inline constinit CommandDesc secretCmd = {
        .needsContext = true,
        .name = "secret",
        .description = "Manage secrets in NVS",
        .args = {},
        .handler = handleRoot,
        .subcommands = subcommands,
    };

    static constexpr std::span<CommandDesc *> commands() {
        static auto commands = std::to_array<CommandDesc *>({&secretCmd});
        return commands;
    }
};

} // namespace Totem::SecretStorage::detail
