#pragma once

#include "Bluetooth/Interfaces/Device.hpp"
#include "StaticConfig/Bluetooth.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>

namespace Totem::Bluetooth::detail {

struct QueuedNotification {
    IDeviceDriver *driver{};
    ConnectionInfo connection{};
    uint16_t attributeHandle{};
    bool indication{};
    size_t payloadSize{};
    std::array<std::byte, StaticConfig::Bluetooth::maxNotificationPayloadBytes>
        payload{};
};

using NotificationSink =
    ReturnCode (*)(void *owner, const QueuedNotification &notification);

struct NotificationSinkBinding {
    void *owner{};
    NotificationSink callback{};

    [[nodiscard]] bool valid() const {
        return owner != nullptr && callback != nullptr;
    }

    ReturnCode push(const QueuedNotification &notification) const {
        if (!valid()) {
            return ERR(InvalidState);
        }
        return callback(owner, notification);
    }
};

} // namespace Totem::Bluetooth::detail
