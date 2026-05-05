#pragma once

#include <cstddef>
#include <string_view>

namespace Totem::FileSystem {

struct FileInfo {
    // Views are valid only for the duration of the list callback.
    std::string_view path;
    std::string_view name;
    std::size_t size = 0;
    bool directory = false;
};

struct StorageInfo {
    std::size_t totalBytes = 0;
    std::size_t usedBytes = 0;
};

} // namespace Totem::FileSystem
