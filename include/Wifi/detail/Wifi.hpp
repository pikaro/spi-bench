#pragma once

#include "Base/HasCommands.hpp"
#include "Base/HasLifecycle.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "SecretStorage/Interfaces/Secret.hpp"
#include "Types/Error.hpp"
#include "Wifi/Interfaces/Config.hpp"
#include "Wifi/Interfaces/Types.hpp"
#include "Wifi/detail/Commands.hpp"
#include "Wifi/detail/PlatformSelect.hpp"
#include <string_view>

namespace Totem::Wifi::detail {

class Wifi : public HasLifecycle<Wifi, Config>,
             public HasCommands<Wifi, Commands<Wifi>> {
    friend class HasLifecycle<Wifi, Config>;
    friend struct LifecycleContract<Wifi, Config>;

  public:
    DELETE_COPY(Wifi)
    DELETE_MOVE(Wifi)

    Wifi() = default;

    static constexpr const char *name = "Wifi";

    [[nodiscard]] Status status() const { return _platform.status(); }

  private:
    ReturnCode _onBegin() {
        auto platformResult = _beginPlatform();
        if (!platformResult.ok()) {
            return platformResult;
        }

        const auto commandResult = _registerCommands();
        if (!commandResult.ok()) {
            (void)_platform.end();
            return commandResult;
        }
        return OK();
    }

    ReturnCode _beginPlatform() {
        if (config().mode == Mode::Disabled) {
            return _platform.begin(config(), {});
        }

        const auto *secretName =
            config().mode == Mode::Station
                ? config().station->credentials.passwordSecretName
                : config().accessPoint->credentials.passwordSecretName;

        auto passwordSecret =
            SecretStorage::Secret<maxPasswordLength, minPasswordLength>(
                secretName);
        FAIL_IF_ERR_FWD(passwordSecret.read(),
                        "Failed to read WiFi password secret %s", secretName);

        const auto passwordView = std::string_view{
            reinterpret_cast<const char *>(passwordSecret.view().data()),
            passwordSecret.size()};
        return _platform.begin(config(), passwordView);
    }

    ReturnCode _onEnd() {
        auto result = _deregisterCommands();
        result.combine(_platform.end());
        return result;
    }

    SelectedPlatform _platform;

    static constexpr LogComponent logComponent = LogComponent::System;
};

inline constexpr LifecycleContract<Wifi, Config> _wifi_lifecycle;
inline constexpr CommandsContract<Wifi, Commands<Wifi>>
    _wifi_commands_contract;

} // namespace Totem::Wifi::detail
