#pragma once

#include "Macros/Facade.hpp"
#include "SecretStorage/Interfaces/IStorage.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>

class SecretService {
  public:
    static void set(Totem::SecretStorage::IStorage &storage) {
        _storage = &storage;
    }

    [[nodiscard]] static bool configured() { return _storage != nullptr; }

    [[nodiscard]] static std::expected<std::size_t, ReturnCode>
    size(std::string_view key) {
        FAIL_IF_NULL(_storage, std::unexpected(ERR(CoreError, InvalidState)),
                     "Secret service not bound");
        return _storage->size(key);
    }

    [[nodiscard]] static std::expected<std::size_t, ReturnCode>
    get(std::string_view key, std::span<std::byte> out) {
        FAIL_IF_NULL(_storage, std::unexpected(ERR(CoreError, InvalidState)),
                     "Secret service not bound");
        return _storage->get(key, out);
    }

    template <typename T>
        requires std::is_trivially_copyable_v<T> &&
                 std::is_default_constructible_v<T>
    [[nodiscard]] static std::expected<T, ReturnCode>
    get(std::string_view key) {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(storedSize, size(key),
                                          "Failed to size secret " SV_FMT,
                                          SV_ARG(key));
        if (storedSize != sizeof(T)) {
            return std::unexpected(ERR(CoreError, InvalidSize));
        }

        T value{};
        auto out = std::as_writable_bytes(std::span{&value, std::size_t{1}});
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(bytesRead, get(key, out),
                                          "Failed to read secret " SV_FMT,
                                          SV_ARG(key));
        if (bytesRead != sizeof(T)) {
            return std::unexpected(ERR(CoreError, InvalidSize));
        }
        return value;
    }

  private:
    static inline Totem::SecretStorage::IStorage *_storage = nullptr;
};
