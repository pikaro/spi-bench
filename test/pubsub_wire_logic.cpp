#include "pubsub_wire.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

using namespace Totem::Tools::PubSubUdp;

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        failures++;
    }
}

} // namespace

int main() {
    static_assert(headerSize == 25);
    static_assert(static_cast<uint16_t>(NodeId::Power) == 0x0080U);
    static_assert(static_cast<uint16_t>(NodeId::Host) == 0x8000U);

    constexpr std::array payload{std::byte{0x12}, std::byte{0x34}};
    auto header = makeHostHeader(42, 0xA5A5U, TrafficClass::Critical);
    auto encoded = encodeFrame(header, payload);
    std::string error;
    auto decoded = decodeFrame(encoded, error);

    expect(decoded.has_value(), "16-bit host frame round trip");
    if (decoded) {
        expect(decoded->header.source == 0x8000U,
               "host source retains the high bit");
        expect(decoded->header.payloadSize == payload.size(),
               "payload size survives round trip");
        expect(decoded->payload.size() == payload.size() &&
                   decoded->payload[0] == payload[0] &&
                   decoded->payload[1] == payload[1],
               "payload survives round trip");
    }

    Header powerHeader{
        .timestampMs = 7,
        .timestampUs = 8,
        .messageId = 9,
        .topic = 10,
        .source = static_cast<uint16_t>(NodeId::Power),
        .trafficClass = static_cast<uint8_t>(TrafficClass::Noncritical),
        .payloadSize = 0,
    };
    encoded = encodeFrame(powerHeader, {});
    decoded = decodeFrame(encoded, error);
    expect(decoded.has_value() && decoded->header.source == 0x0080U,
           "power source survives 16-bit frame round trip");

    if (failures != 0) {
        return 1;
    }
    std::cout << "PubSub host wire tests passed\n";
    return 0;
}
