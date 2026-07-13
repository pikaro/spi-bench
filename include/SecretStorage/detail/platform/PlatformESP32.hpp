// IWYU pragma: private
// IWYU pragma: friend "SecretStorage/detail/.*"

#pragma once

#include "SecretStorage/Interfaces/Config.hpp"
#include "SecretStorage/Interfaces/Entry.hpp"
#include "Types/Error.hpp"
#include "nvs.h"
#include "nvs_flash.h"
#include <array>
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>
#include <utility>

namespace Totem::SecretStorage::detail {

class PlatformESP32 {
  public:
    static constexpr std::size_t maxKeyLength = NVS_KEY_NAME_MAX_SIZE - 1U;

    ReturnCode begin(const Config &config) {
        auto err = nvs_flash_init_partition(config.partitionName);
        if (err != ESP_OK) {
            return mapError(err);
        }

        _partitionName = config.partitionName;
        err = nvs_open_from_partition(_partitionName, nvsNamespace,
                                      NVS_READWRITE, &_handle);
        if (err != ESP_OK) {
            (void)nvs_flash_deinit_partition(_partitionName);
            _partitionName = nullptr;
            return mapError(err);
        }
        return OK();
    }

    ReturnCode end() {
        nvs_close(_handle);
        _handle = 0;

        const auto err = nvs_flash_deinit_partition(_partitionName);
        _partitionName = nullptr;
        return mapError(err);
    }

    std::expected<std::size_t, ReturnCode> size(std::string_view key) const {
        auto nvsKey = makeKey(key);
        if (!nvsKey) {
            return std::unexpected(nvsKey.error());
        }

        std::size_t valueSize = 0;
        const auto err =
            nvs_get_blob(_handle, nvsKey->data(), nullptr, &valueSize);
        if (err != ESP_OK) {
            return std::unexpected(mapError(err));
        }
        return valueSize;
    }

    std::expected<std::size_t, ReturnCode> get(std::string_view key,
                                               std::span<std::byte> out) const {
        auto nvsKey = makeKey(key);
        if (!nvsKey) {
            return std::unexpected(nvsKey.error());
        }

        std::size_t valueSize = out.size();
        const auto err =
            nvs_get_blob(_handle, nvsKey->data(), out.data(), &valueSize);
        if (err != ESP_OK) {
            return std::unexpected(mapError(err));
        }
        return valueSize;
    }

    ReturnCode set(std::string_view key, std::span<const std::byte> value) {
        auto nvsKey = makeKey(key);
        if (!nvsKey) {
            return nvsKey.error();
        }

        auto err =
            nvs_set_blob(_handle, nvsKey->data(), value.data(), value.size());
        if (err != ESP_OK) {
            return mapError(err);
        }
        return mapError(nvs_commit(_handle));
    }

    template <typename Visitor> ReturnCode list(Visitor &&visitor) const {
        nvs_iterator_t iterator = nullptr;
        auto err = nvs_entry_find_in_handle(_handle, NVS_TYPE_BLOB, &iterator);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            return OK();
        }
        if (err != ESP_OK) {
            return mapError(err);
        }

        while (err == ESP_OK) {
            nvs_entry_info_t info{};
            err = nvs_entry_info(iterator, &info);
            if (err != ESP_OK) {
                nvs_release_iterator(iterator);
                return mapError(err);
            }

            std::size_t valueSize = 0;
            err = nvs_get_blob(_handle, info.key, nullptr, &valueSize);
            if (err != ESP_OK) {
                nvs_release_iterator(iterator);
                return mapError(err);
            }

            const Entry entry{
                .key = info.key,
                .size = valueSize,
            };
            const auto visitResult = std::forward<Visitor>(visitor)(entry);
            if (!visitResult.ok()) {
                nvs_release_iterator(iterator);
                return visitResult;
            }

            err = nvs_entry_next(&iterator);
        }

        nvs_release_iterator(iterator);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            return OK();
        }
        return mapError(err);
    }

  private:
    using KeyBuffer = std::array<char, NVS_KEY_NAME_MAX_SIZE>;

    static constexpr const char *nvsNamespace = "secrets";

    static std::expected<KeyBuffer, ReturnCode> makeKey(std::string_view key) {
        if (key.empty() || key.size() > maxKeyLength ||
            key.find('\0') != std::string_view::npos) {
            return std::unexpected(ERR(CoreError, InvalidArgument));
        }

        KeyBuffer out{};
        for (std::size_t i = 0; i < key.size(); ++i) {
            out[i] = key[i];
        }
        return out;
    }

    static ReturnCode mapError(esp_err_t err) {
        switch (err) {
        case ESP_OK:
            return OK();
        case ESP_ERR_NVS_NOT_FOUND:
        case ESP_ERR_NVS_PART_NOT_FOUND:
            return ERR(CoreError, NotFound);
        case ESP_ERR_NVS_INVALID_NAME:
        case ESP_ERR_NVS_KEY_TOO_LONG:
            return ERR(CoreError, InvalidArgument);
        case ESP_ERR_NVS_TYPE_MISMATCH:
            return ERR(CoreError, InvalidData);
        case ESP_ERR_NVS_INVALID_LENGTH:
        case ESP_ERR_NVS_VALUE_TOO_LONG:
            return ERR(CoreError, InvalidSize);
        case ESP_ERR_NVS_NOT_ENOUGH_SPACE:
        case ESP_ERR_NVS_NO_FREE_PAGES:
            return ERR(CoreError, OutOfMemory);
        case ESP_ERR_NVS_READ_ONLY:
            return ERR(CoreError, Forbidden);
        case ESP_ERR_NVS_NOT_INITIALIZED:
        case ESP_ERR_NVS_INVALID_HANDLE:
        case ESP_ERR_NVS_INVALID_STATE:
            return ERR(CoreError, InvalidState);
        default:
            return ERR(CoreError, OperationFailed);
        }
    }

    const char *_partitionName = nullptr;
    nvs_handle_t _handle = 0;
};

} // namespace Totem::SecretStorage::detail
