#pragma once

#include "Bluetooth/Interfaces/Types.hpp"

namespace Totem::Bluetooth {

class IDeviceDriver {
  public:
    virtual ~IDeviceDriver() = default;

    [[nodiscard]] virtual const char *driverName() const = 0;
    [[nodiscard]] virtual const char *serviceUuid() const = 0;
    [[nodiscard]] virtual const char *characteristicUuid() const = 0;

    [[nodiscard]] virtual bool matches(const AdvertisedDevice &device) const {
        (void)device;
        return true;
    }

    virtual void onConnected(const ConnectionInfo &connection) {
        (void)connection;
    }

    virtual void onSubscribed(const SubscriptionInfo &subscription) {
        (void)subscription;
    }

    virtual void onDisconnected(const ConnectionInfo &connection, int reason) {
        (void)connection;
        (void)reason;
    }

    virtual void onNotification(const Notification &notification) = 0;
};

} // namespace Totem::Bluetooth
