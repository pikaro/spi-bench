#pragma once

#include "CommandBackend/Facade.hpp"
#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "FileSystem/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Services/Commands.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <string_view>

namespace Totem::FileSystem::detail {

using DefaultFileSystem = Totem::FileSystem::FileSystem<>;

inline ReturnCode logFileSystemPath(DefaultFileSystem &fileSystem,
                                    std::string_view path, std::size_t depth,
                                    std::size_t &entryCount,
                                    std::size_t &fileCount,
                                    std::size_t &byteCount) {
    return fileSystem.forEachEntry(
        path, [&fileSystem, depth, &entryCount, &fileCount,
               &byteCount](const Totem::FileSystem::FileInfo &entry) {
            ++entryCount;
            if (entry.directory) {
                _log_i("LittleFS dir : " SV_FMT, SV_ARG(entry.path));
                if (depth + 1 >= Totem::FileSystem::Config::bootListMaxDepth) {
                    _log_w("LittleFS listing depth limit reached at " SV_FMT,
                           SV_ARG(entry.path));
                    return OK();
                }
                return logFileSystemPath(fileSystem, entry.path, depth + 1,
                                         entryCount, fileCount, byteCount);
            }

            FAIL_IF(static_cast<std::size_t>(-1) - byteCount < entry.size,
                    ERR(CoreError, Overflow),
                    "LittleFS file byte count overflow while listing " SV_FMT,
                    SV_ARG(entry.path));

            ++fileCount;
            byteCount += entry.size;
            _log_i("LittleFS file: " SV_FMT " (%zu bytes)", SV_ARG(entry.path),
                   entry.size);
            return OK();
        });
}

inline ReturnCode logFileSystemContents(DefaultFileSystem &fileSystem,
                                        std::string_view path = "/") {
    std::size_t entryCount = 0;
    std::size_t fileCount = 0;
    std::size_t byteCount = 0;

    _log_i("LittleFS contents at %s path " SV_FMT ":", Config::basePath,
           SV_ARG(path));
    FAIL_IF_ERR_FWD(logFileSystemPath(fileSystem, path, 0, entryCount,
                                      fileCount, byteCount),
                    "LittleFS directory listing failed");

    auto storageInfo = fileSystem.info();
    if (!storageInfo) {
        return storageInfo.error();
    }

    if (entryCount == 0) {
        _log_i("LittleFS is empty");
    }
    _log_i("LittleFS summary: entries=%zu files=%zu fileBytes=%zu used=%zu "
           "total=%zu",
           entryCount, fileCount, byteCount, storageInfo->usedBytes,
           storageInfo->totalBytes);
    return OK();
}

inline ReturnCode handleLs(CommandDesc::ParsedArgs args, void *ctx) {
    auto *fileSystem = static_cast<DefaultFileSystem *>(ctx);
    FAIL_IF_NULL(fileSystem, ERR(CoreError, InvalidArgument),
                 "FileSystem /ls command context is null");

    auto pathArg = args.get<std::string_view>(0);
    FAIL_IF(!pathArg.ok && pathArg.error != CommandDesc::ArgError::Missing,
            ERR(CommandError, BadArgument),
            "Invalid path argument for /ls command");

    const auto path = pathArg.ok ? pathArg.value : std::string_view{"/"};
    return logFileSystemContents(*fileSystem, path);
}

inline CommandDesc lsCmd = {
    .needsContext = true,
    .name = "ls",
    .description = "List LittleFS contents",
    .args =
        {
            CommandBackend::arg<std::string_view>(
                "path", CommandDesc::ArgRequirement::Optional),
        },
    .handler = handleLs,
    .subcommands = {},
};

inline ReturnCode registerCommands(DefaultFileSystem &fileSystem) {
    auto &registrar = CommandRegistrarService::get();
    FAIL_IF_UNEXPECTED_FWD(_, registrar.registerCommand(lsCmd, &fileSystem),
                           "Failed to register /ls command");
    return OK();
}

} // namespace Totem::FileSystem::detail
