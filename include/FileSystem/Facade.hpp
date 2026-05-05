#pragma once

#include "FileSystem/Interfaces/Config.hpp" // IWYU pragma: export
#include "FileSystem/Interfaces/Types.hpp"  // IWYU pragma: export
#include "FileSystem/detail/BackendSelect.hpp"
#include "FileSystem/detail/FileChunkReader.hpp"
#include "FileSystem/detail/FileSystem.hpp"

namespace Totem::FileSystem {

template <class Implementation = detail::SelectedImplementation>
using FileSystem = detail::FileSystem<Implementation>;

using LittleFS = detail::LittleFS;

template <std::size_t ChunkSize = Config::defaultChunkSize>
using FileChunkReader =
    detail::BasicFileChunkReader<typename detail::SelectedImplementation::File,
                                 ChunkSize>;

} // namespace Totem::FileSystem
