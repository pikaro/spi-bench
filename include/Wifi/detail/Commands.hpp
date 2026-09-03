// IWYU pragma: private

#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <span>

namespace Totem::Wifi::detail {

static constexpr LogComponent logComponent = LogComponent::System;

template <typename Owner> struct Commands {
    static ReturnCode handleWifiStatus(CommandDesc::ParsedArgs /*args*/,
                                       void *ctx) {
        auto *wifi = static_cast<Owner *>(ctx);
        FAIL_IF_NULL(wifi, ERR(CoreError, InvalidArgument),
                     "WiFi command context is null");

        const auto status = wifi->status();
        const auto ip = status.stationIpv4.octets();
        _log_i("WiFi status mode=%u started=%u apStarted=%u apStations=%u "
               "staConnected=%u staIp=%u.%u.%u.%u",
               static_cast<unsigned>(status.mode), status.started ? 1U : 0U,
               status.accessPointStarted ? 1U : 0U,
               static_cast<unsigned>(status.accessPointStationCount),
               status.stationConnected ? 1U : 0U,
               status.stationIpv4.valid ? static_cast<unsigned>(ip[0]) : 0U,
               status.stationIpv4.valid ? static_cast<unsigned>(ip[1]) : 0U,
               status.stationIpv4.valid ? static_cast<unsigned>(ip[2]) : 0U,
               status.stationIpv4.valid ? static_cast<unsigned>(ip[3]) : 0U);
        return OK();
    }

    static inline CommandDesc wifiStatusCmd = {
        .needsContext = true,
        .name = "wifi",
        .description = "Print WiFi status",
        .args = {},
        .handler = handleWifiStatus,
        .subcommands = {},
    };

    static constexpr std::span<CommandDesc *> commands() {
        static auto commands = std::to_array<CommandDesc *>({
            &wifiStatusCmd,
        });
        return commands;
    }
};

} // namespace Totem::Wifi::detail
