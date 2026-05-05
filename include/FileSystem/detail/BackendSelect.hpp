#pragma once

#include "StaticConfig/FileSystem.hpp"

#if FILESYSTEM_USE_LITTLEFS
#include "FileSystem/detail/LittleFS.hpp"
#else
#error "Only the LittleFS filesystem backend is supported"
#endif

namespace Totem::FileSystem::detail {

#if FILESYSTEM_USE_LITTLEFS
using SelectedImplementation = LittleFS;
#endif

} // namespace Totem::FileSystem::detail
