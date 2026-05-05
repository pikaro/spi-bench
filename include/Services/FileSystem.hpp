#pragma once

#include "FileSystem/Facade.hpp"
#include "Macros/Facade.hpp"

class FileSystemService {
  public:
    using DefaultFileSystem = Totem::FileSystem::FileSystem<>;

    static void set(DefaultFileSystem &fileSystem) {
        _fileSystem = &fileSystem;
    }

    [[nodiscard]] static bool configured() { return _fileSystem != nullptr; }

    static DefaultFileSystem &get() {
        ABORT_IF_NULL(_fileSystem, "FileSystem service not bound");
        return *_fileSystem;
    }

  private:
    static inline DefaultFileSystem *_fileSystem = nullptr;
};
