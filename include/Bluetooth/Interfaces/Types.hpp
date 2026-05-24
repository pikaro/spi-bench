#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

namespace Totem::Bluetooth {

struct Address {
    uint8_t type{};
    std::array<uint8_t, 6> value{};
};

inline void formatAddress(const Address &address, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0) {
        return;
    }
    std::snprintf(out, outSize, "%02x:%02x:%02x:%02x:%02x:%02x",
                  address.value[5], address.value[4], address.value[3],
                  address.value[2], address.value[1], address.value[0]);
}

struct AdvertisedDevice {
    Address address{};
    int8_t rssi{};
    bool connectable{};
};

struct ConnectionInfo {
    Address address{};
    uint16_t connHandle{};
    int8_t rssi{};
};

struct SubscriptionInfo {
    ConnectionInfo connection{};
    uint16_t attributeHandle{};
};

struct Notification {
    ConnectionInfo connection{};
    uint16_t attributeHandle{};
    bool indication{};
    std::span<const std::byte> payload{};
};

} // namespace Totem::Bluetooth
