#pragma once

#include "CommandBackend/Facade.hpp"
#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "FileSystem/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Services/Commands.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <cstdint>
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

    if (fileSystem.exists(path) && !fileSystem.isDir(path)) {
        FAIL_IF_UNEXPECTED_FWD(size, fileSystem.fileSize(path),
                               "Failed to stat LittleFS file " SV_FMT,
                               SV_ARG(path));
        byteCount = size;
        fileCount = 1;
        entryCount = 1;
        _log_i("LittleFS file: " SV_FMT " (%zu bytes)", SV_ARG(path), size);

        auto storageInfo = fileSystem.info();
        if (!storageInfo) {
            return storageInfo.error();
        }

        _log_i("LittleFS summary: entries=%zu files=%zu fileBytes=%zu used=%zu "
               "total=%zu",
               entryCount, fileCount, byteCount, storageInfo->usedBytes,
               storageInfo->totalBytes);
        return OK();
    }

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

inline ReturnCode optionalU32(CommandDesc::ParsedArgs args, size_t index,
                              uint32_t defaultValue, uint32_t &out) {
    auto parsed = args.get<uint32_t>(index);
    if (parsed.ok) {
        out = parsed.value;
        return OK();
    }
    if (parsed.error == CommandDesc::ArgError::Missing) {
        out = defaultValue;
        return OK();
    }
    return ERR(CommandError, BadArgument);
}

inline ReturnCode handleCat(CommandDesc::ParsedArgs args, void *ctx) {
    auto *fileSystem = static_cast<DefaultFileSystem *>(ctx);
    FAIL_IF_NULL(fileSystem, ERR(CoreError, InvalidArgument),
                 "FileSystem /cat command context is null");

    auto pathArg = args.get<std::string_view>(0);
    FAIL_IF(!pathArg.ok, ERR(CommandError, BadArgument),
            "Missing or invalid path argument for /cat command");

    uint32_t offsetArg = 0;
    uint32_t maxBytesArg = 4096;
    FAIL_IF_ERR_FWD(optionalU32(args, 1, 0, offsetArg),
                    "Invalid offset argument for /cat command");
    FAIL_IF_ERR_FWD(optionalU32(args, 2, 4096, maxBytesArg),
                    "Invalid max-bytes argument for /cat command");

    const auto path = pathArg.value;
    FAIL_IF_UNEXPECTED_FWD(fileBytes, fileSystem->fileSize(path),
                           "Failed to stat LittleFS file " SV_FMT,
                           SV_ARG(path));
    FAIL_IF(offsetArg > fileBytes, ERR(CommandError, BadArgument),
            "/cat offset beyond end of file");

    auto bytesToDump = fileBytes - offsetArg;
    if (maxBytesArg < bytesToDump) {
        bytesToDump = maxBytesArg;
    }

    _log_i("LittleFS dump begin path=" SV_FMT " size=%zu offset=%lu bytes=%zu",
           SV_ARG(path), fileBytes, static_cast<unsigned long>(offsetArg),
           bytesToDump);

    DefaultFileSystem::ChunkReader<128> reader{};
    FAIL_IF_ERR_FWD(fileSystem->openReader(reader, path),
                    "Failed to open LittleFS file " SV_FMT, SV_ARG(path));

    size_t consumed = 0;
    size_t emitted = 0;

    while (emitted < bytesToDump) {
        auto next = reader.readNext();
        if (!next) {
            return next.error();
        }
        if (!*next) {
            break;
        }

        const auto span = reader.span();
        const auto chunkEnd = consumed + span.size();
        if (chunkEnd <= offsetArg) {
            consumed = chunkEnd;
            continue;
        }

        const auto start =
            offsetArg > consumed ? offsetArg - consumed : size_t{0};
        auto take = span.size() - start;
        if (take > bytesToDump - emitted) {
            take = bytesToDump - emitted;
        }
        const auto *data =
            reinterpret_cast<const char *>(span.data() + start);
        _log_i("LittleFS dump: %.*s", static_cast<int>(take), data);
        emitted += take;
        consumed = chunkEnd;
    }

    FAIL_IF_ERR_FWD(reader.close(), "Failed to close LittleFS dump reader");
    _log_i("LittleFS dump end path=" SV_FMT " nextOffset=%lu eof=%u",
           SV_ARG(path),
           static_cast<unsigned long>(offsetArg +
                                      static_cast<uint32_t>(emitted)),
           (offsetArg + emitted) >= fileBytes ? 1U : 0U);
    return OK();
}

inline ReturnCode handleRm(CommandDesc::ParsedArgs args, void *ctx) {
    auto *fileSystem = static_cast<DefaultFileSystem *>(ctx);
    FAIL_IF_NULL(fileSystem, ERR(CoreError, InvalidArgument),
                 "FileSystem /rm command context is null");

    auto pathArg = args.get<std::string_view>(0);
    FAIL_IF(!pathArg.ok, ERR(CommandError, BadArgument),
            "Missing or invalid path argument for /rm command");

    FAIL_IF_ERR_FWD(fileSystem->removeFile(pathArg.value),
                    "Failed to remove LittleFS path " SV_FMT,
                    SV_ARG(pathArg.value));
    _log_i("LittleFS removed: " SV_FMT, SV_ARG(pathArg.value));
    return OK();
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

inline CommandDesc catCmd = {
    .needsContext = true,
    .name = "cat",
    .description = "Dump a LittleFS file",
    .args =
        {
            CommandBackend::arg<std::string_view>("path"),
            CommandBackend::arg<uint32_t>(
                "offset", CommandDesc::ArgRequirement::Optional),
            CommandBackend::arg<uint32_t>(
                "maxBytes", CommandDesc::ArgRequirement::Optional),
        },
    .handler = handleCat,
    .subcommands = {},
};

inline CommandDesc rmCmd = {
    .needsContext = true,
    .name = "rm",
    .description = "Remove a LittleFS file",
    .args =
        {
            CommandBackend::arg<std::string_view>("path"),
        },
    .handler = handleRm,
    .subcommands = {},
};

inline ReturnCode registerCommands(DefaultFileSystem &fileSystem) {
    auto &registrar = CommandRegistrarService::get();
    FAIL_IF_UNEXPECTED_FWD(lsKey, registrar.registerCommand(lsCmd, &fileSystem),
                           "Failed to register /ls command");
    (void)lsKey;
    FAIL_IF_UNEXPECTED_FWD(catKey,
                           registrar.registerCommand(catCmd, &fileSystem),
                           "Failed to register /cat command");
    (void)catKey;
    FAIL_IF_UNEXPECTED_FWD(rmKey, registrar.registerCommand(rmCmd, &fileSystem),
                           "Failed to register /rm command");
    (void)rmKey;
    return OK();
}

} // namespace Totem::FileSystem::detail
