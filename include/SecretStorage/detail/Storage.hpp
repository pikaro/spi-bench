#pragma once

#include "Base/HasCommands.hpp"
#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "SecretStorage/Interfaces/Config.hpp"
#include "SecretStorage/Interfaces/Entry.hpp"
#include "SecretStorage/Interfaces/IStorage.hpp"
#include "SecretStorage/detail/Commands.hpp"
#include "SecretStorage/detail/Types.hpp"
#include "SecretStorage/detail/platform/PlatformSelect.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Totem::SecretStorage::detail {

class Storage : public IStorage,
                public HasLifecycle<Storage, Config>,
                public HasCommands<Storage, Commands<Storage>> {
    friend class HasLifecycle<Storage, Config>;
    friend struct Commands<Storage>;
    friend struct LifecycleContract<Storage, Config>;

  public:
    DELETE_COPY(Storage)
    DELETE_MOVE(Storage)

    Storage() = default;

    static constexpr const char *name = "SecretStorage";
    static constexpr LogComponent logComponent = detail::logComponent;
    static constexpr std::size_t maxKeyLength = Platform::maxKeyLength;

    [[nodiscard]] std::expected<std::size_t, ReturnCode>
    size(std::string_view key) const override {
        FAIL_IF_INACTIVE_UNEXPECTED("Cannot size a secret before begin");
        return _platform.size(key);
    }

    [[nodiscard]] std::expected<std::size_t, ReturnCode>
    get(std::string_view key, std::span<std::byte> out) const override {
        FAIL_IF_INACTIVE_UNEXPECTED("Cannot read a secret before begin");
        return _platform.get(key, out);
    }

    template <typename T>
        requires std::is_trivially_copyable_v<T> &&
                 std::is_default_constructible_v<T>
    [[nodiscard]] std::expected<T, ReturnCode> get(std::string_view key) const {
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

    ReturnCode set(std::string_view key, std::span<const std::byte> value) {
        FAIL_IF_INACTIVE_ERR("Cannot store a secret before begin");
        return _platform.set(key, value);
    }

    template <typename T>
        requires std::is_trivially_copyable_v<T>
    ReturnCode set(std::string_view key, const T &value) {
        return set(key, std::as_bytes(std::span{&value, std::size_t{1}}));
    }

    ReturnCode erase(std::string_view key) {
        FAIL_IF_INACTIVE_ERR("Cannot delete a secret before begin");
        return _platform.erase(key);
    }

    template <typename Visitor> ReturnCode list(Visitor &&visitor) const {
        FAIL_IF_INACTIVE_ERR("Cannot list secrets before begin");
        return _platform.list(std::forward<Visitor>(visitor));
    }

  private:
    ReturnCode _onBegin() {
        _commandsSealed = false;
        FAIL_IF_ERR_FWD(_platform.begin(config()),
                        "Failed to open secret NVS partition %s",
                        config().partitionName);
        const auto commandResult = _registerCommands();
        if (!commandResult.ok()) {
            (void)_platform.end();
            return commandResult;
        }
        return OK();
    }

    ReturnCode _onEnd() {
        auto result = _deregisterCommands();
        const auto platformResult = _platform.end();
        result.combine(platformResult);
        return result;
    }

    Platform _platform{};
    // Runtime command gate only; storage APIs intentionally ignore this state.
    bool _commandsSealed = false;
};

inline constexpr CommandsContract<Storage, Commands<Storage>> commandsContract;
inline constexpr LifecycleContract<Storage, Config> lifecycleContract;

} // namespace Totem::SecretStorage::detail
