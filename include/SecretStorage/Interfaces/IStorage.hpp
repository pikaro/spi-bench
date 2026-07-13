#pragma once

#include "Types/Error.hpp"
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>

namespace Totem::SecretStorage {

struct IStorage {
    virtual ~IStorage() = default;

    [[nodiscard]] virtual std::expected<std::size_t, ReturnCode>
    size(std::string_view key) const = 0;

    [[nodiscard]] virtual std::expected<std::size_t, ReturnCode>
    get(std::string_view key, std::span<std::byte> out) const = 0;
};

} // namespace Totem::SecretStorage
