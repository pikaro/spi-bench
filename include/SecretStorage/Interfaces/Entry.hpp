#pragma once

#include <cstddef>
#include <string_view>

namespace Totem::SecretStorage {

struct Entry {
    // The key view is valid only for the duration of the list callback.
    std::string_view key;
    std::size_t size;
};

} // namespace Totem::SecretStorage
