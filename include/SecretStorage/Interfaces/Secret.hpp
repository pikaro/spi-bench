#pragma once

#include "Macros/Facade.hpp"
#include "Services/Secret.hpp"
#include "Types/Error.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <expected>
#include <span>

namespace Totem::SecretStorage {

template <std::size_t MaxN, std::size_t MinN = 0> struct Secret {
    static_assert(MinN <= MaxN);

    Secret(const char *secretName) : name(secretName) {}
    const char *name = nullptr;

    [[nodiscard]] bool validate() const {
        if (name == nullptr || name[0] == '\0') {
            return false;
        }
        return true;
    }

    [[nodiscard]] std::size_t size() const { return _size; }

    [[nodiscard]] std::span<const std::byte> view() const {
        return std::span<const std::byte>{_value}.first(size());
    }

    [[nodiscard]] std::span<std::byte> view() {
        return std::span<std::byte>{_value}.first(size());
    }

    [[nodiscard]] bool isSet() const {
        return _isSet.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::expected<std::span<const std::byte>, ReturnCode> get() {
        if (!isSet()) {
            const auto result = read();
            if (!result.ok()) {
                return std::unexpected(result);
            }
        }
        return view();
    }

    ReturnCode read() {
        FAIL_IF(!validate(), ERR(CoreError, InvalidArgument),
                "Secret %s is not valid", name);
        FAIL_IF_UNEXPECTED_FWD(secretSize, SecretService::size(name),
                               "Secret %s is not set", name);
        FAIL_IF(secretSize < MinN || secretSize > MaxN,
                ERR(CoreError, InvalidSize),
                "Secret %s must contain %zu-%zu bytes", name,
                static_cast<std::size_t>(MinN), static_cast<std::size_t>(MaxN));

        FAIL_IF_UNEXPECTED_FWD(
            bytesRead,
            SecretService::get(name,
                               std::span<std::byte>{_value}.first(secretSize)),
            "Failed to read secret %s", name);
        FAIL_IF(bytesRead != secretSize, ERR(CoreError, InvalidSize),
                "Secret %s changed size while being read", name);

        _size = bytesRead;
        _isSet.store(true, std::memory_order_release);
        return OK();
    }

  private:
    std::atomic<bool> _isSet{false};
    std::array<std::byte, MaxN> _value{};
    std::size_t _size = 0;
};

} // namespace Totem::SecretStorage
