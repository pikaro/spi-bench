#pragma once

#include <cstddef>

namespace Totem::SecretStorage {

struct Config {
    static constexpr std::size_t maxPartitionNameLength = 16;

    // ESP-IDF retains this pointer while the partition is initialized. Use a
    // string with static lifetime, as in the default below.
    const char *partitionName = "secrets";

    [[nodiscard]] constexpr bool validate() const {
        if (partitionName == nullptr || partitionName[0] == '\0') {
            return false;
        }

        for (std::size_t i = 1; i <= maxPartitionNameLength; ++i) {
            if (partitionName[i] == '\0') {
                return true;
            }
        }
        return false;
    }
};

} // namespace Totem::SecretStorage
