#pragma once

#include <cstddef>

#ifndef FILESYSTEM_USE_LITTLEFS
#define FILESYSTEM_USE_LITTLEFS 1
#endif

struct FileSystemConfig {
    static constexpr const char *basePath = "/littlefs";
    static constexpr const char *partitionLabel = "littlefs";

    static constexpr std::size_t maxPathLength = 160;
    static constexpr std::size_t defaultChunkSize = 2048;
    static constexpr std::size_t bootListMaxDepth = 8;

    static constexpr bool formatIfMountFailed = true;
    static constexpr bool dontMount = false;

    [[nodiscard]] static constexpr bool validate() {
        return basePath != nullptr && basePath[0] == '/' &&
               partitionLabel != nullptr && partitionLabel[0] != '\0' &&
               maxPathLength > 1 && defaultChunkSize > 0 &&
               bootListMaxDepth > 0;
    }
};
