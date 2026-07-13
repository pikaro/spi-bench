#pragma once

#include "Base/HasLifecycle.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include "Services/Secret.hpp"
#include "Types/Error.hpp"
#include "Wifi/Interfaces/Config.hpp"
#include "Wifi/Interfaces/Types.hpp"
#include "Wifi/detail/PlatformSelect.hpp"
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace Totem::Wifi::detail {

class Wifi : public HasLifecycle<Wifi, Config> {
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
        if (config().mode == Mode::Disabled) {
            return _platform.begin(config(), {});
        }

        const auto *secretName =
            config().mode == Mode::Station
                ? config().station->credentials.passwordSecretName
                : config().accessPoint->credentials.passwordSecretName;
        FAIL_IF_UNEXPECTED_FWD(passwordSize, SecretService::size(secretName),
                               "WiFi password secret %s is not set",
                               secretName);
        FAIL_IF(passwordSize < 8 || passwordSize > detail::maxPasswordLength,
                ERR(CoreError, InvalidSize),
                "WiFi password secret %s must contain 8-%zu bytes", secretName,
                detail::maxPasswordLength);

        std::array<std::byte, detail::maxPasswordLength> password{};
        FAIL_IF_UNEXPECTED_FWD(
            bytesRead,
            SecretService::get(
                secretName, std::span<std::byte>{password}.first(passwordSize)),
            "Failed to read WiFi password secret %s", secretName);
        FAIL_IF(bytesRead != passwordSize, ERR(CoreError, InvalidSize),
                "WiFi password secret %s changed size while being read",
                secretName);

        const auto passwordView = std::string_view{
            reinterpret_cast<const char *>(password.data()), bytesRead};
        return _platform.begin(config(), passwordView);
    }
    ReturnCode _onEnd() { return _platform.end(); }

    SelectedPlatform _platform;

    static constexpr LogComponent logComponent = LogComponent::System;
};

inline constexpr LifecycleContract<Wifi, Config> _wifi_lifecycle;

} // namespace Totem::Wifi::detail
