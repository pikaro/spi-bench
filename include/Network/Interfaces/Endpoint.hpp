#pragma once

#include <cstdint>

namespace Totem::Network {

struct Ipv4Endpoint {
    uint32_t address = 0;
    uint16_t port = 0;
};

} // namespace Totem::Network
