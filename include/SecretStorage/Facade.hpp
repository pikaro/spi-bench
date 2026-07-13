#pragma once

#include "SecretStorage/Interfaces/Config.hpp"   // IWYU pragma: export
#include "SecretStorage/Interfaces/Entry.hpp"    // IWYU pragma: export
#include "SecretStorage/Interfaces/IStorage.hpp" // IWYU pragma: export
#include "SecretStorage/Interfaces/Secret.hpp"   // IWYU pragma: export
#include "SecretStorage/detail/Storage.hpp"

namespace Totem::SecretStorage {

using detail::Storage;

} // namespace Totem::SecretStorage
