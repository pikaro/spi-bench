#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Totem::Tools::PubSubUdp {

enum class Topic : uint32_t {
    PubSub = 1U << 1,
    Button = 1U << 8,
};

enum class NodeId : uint8_t {
    Host = 1U << 7,
};

enum class TrafficClass : uint8_t {
    Noncritical = 0,
    Critical = 1,
};

enum class SubscribeEventType : uint8_t {
    Register = 0,
    Unregister = 1,
};

enum class ButtonEventType : uint8_t {
    Pressed = 0,
    Released = 1,
};

enum class PeripheralButton : uint8_t {
    Bell = 0,
};

struct Header {
    uint32_t timestampMs = 0;
    uint64_t timestampUs = 0;
    uint32_t messageId = 0;
    uint32_t topic = 0;
    uint8_t source = 0;
    uint8_t trafficClass = 0;
    uint16_t payloadSize = 0;
};

struct PubSubEvent {
    uint32_t topic = 0;
    uint8_t type = 0;
};

struct ButtonEvent {
    uint8_t type = 0;
    uint8_t button = 0;
};

struct Frame {
    Header header{};
    std::span<const std::byte> payload{};
};

inline constexpr size_t headerSize = sizeof(uint32_t) + sizeof(uint64_t) +
                                     sizeof(uint32_t) + sizeof(uint32_t) +
                                     sizeof(uint8_t) + sizeof(uint8_t) +
                                     sizeof(uint16_t);
inline constexpr size_t crcSize = sizeof(uint32_t);
inline constexpr size_t maxPayloadSize = 2048;
inline constexpr std::array<std::byte, 8> keepalivePacket{
    std::byte{0x54}, std::byte{0x50}, std::byte{0x55}, std::byte{0x44},
    std::byte{0x50}, std::byte{0x4B}, std::byte{0x41}, std::byte{0x31},
};

template <typename T>
inline void appendLe(std::vector<std::byte> &out, T value) {
    using Raw = T;
    using Unsigned = std::make_unsigned_t<Raw>;
    auto bits = static_cast<Unsigned>(value);
    for (size_t i = 0; i < sizeof(Raw); ++i) {
        out.push_back(static_cast<std::byte>((bits >> (i * 8U)) & 0xFFU));
    }
}

template <typename T>
inline bool readLe(std::span<const std::byte> in, size_t &offset, T &out) {
    if (offset + sizeof(T) > in.size()) {
        return false;
    }
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned bits = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
        bits |= static_cast<Unsigned>(std::to_integer<uint8_t>(in[offset + i]))
                << (i * 8U);
    }
    offset += sizeof(T);
    out = static_cast<T>(bits);
    return true;
}

inline void appendHeader(std::vector<std::byte> &out, const Header &header) {
    appendLe<uint32_t>(out, header.timestampMs);
    appendLe<uint64_t>(out, header.timestampUs);
    appendLe<uint32_t>(out, header.messageId);
    appendLe<uint32_t>(out, header.topic);
    appendLe<uint8_t>(out, header.source);
    appendLe<uint8_t>(out, header.trafficClass);
    appendLe<uint16_t>(out, header.payloadSize);
}

inline bool readHeader(std::span<const std::byte> data, Header &header) {
    if (data.size() < headerSize) {
        return false;
    }
    size_t offset = 0;
    return readLe<uint32_t>(data, offset, header.timestampMs) &&
           readLe<uint64_t>(data, offset, header.timestampUs) &&
           readLe<uint32_t>(data, offset, header.messageId) &&
           readLe<uint32_t>(data, offset, header.topic) &&
           readLe<uint8_t>(data, offset, header.source) &&
           readLe<uint8_t>(data, offset, header.trafficClass) &&
           readLe<uint16_t>(data, offset, header.payloadSize);
}

inline uint32_t crc32(std::span<const std::byte> data) {
    uint32_t crc = 0xFFFFFFFFU;
    for (const auto value : data) {
        crc ^= std::to_integer<uint8_t>(value);
        for (size_t bit = 0; bit < 8; ++bit) {
            const auto mask = (crc & 1U) ? 0xEDB88320U : 0U;
            crc = (crc >> 1U) ^ mask;
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

inline bool isKeepalive(std::span<const std::byte> datagram) {
    return datagram.size() == keepalivePacket.size() &&
           std::equal(datagram.begin(), datagram.end(),
                      keepalivePacket.begin());
}

inline std::vector<std::byte> encodePayload(const PubSubEvent &event) {
    std::vector<std::byte> out;
    out.reserve(5);
    appendLe<uint32_t>(out, event.topic);
    appendLe<uint8_t>(out, event.type);
    return out;
}

inline std::vector<std::byte> encodePayload(const ButtonEvent &event) {
    std::vector<std::byte> out;
    out.reserve(2);
    appendLe<uint8_t>(out, event.type);
    appendLe<uint8_t>(out, event.button);
    return out;
}

inline std::vector<std::byte> encodeFrame(Header header,
                                          std::span<const std::byte> payload) {
    header.payloadSize = static_cast<uint16_t>(payload.size());
    std::vector<std::byte> out;
    out.reserve(headerSize + payload.size() + crcSize);
    appendHeader(out, header);
    out.insert(out.end(), payload.begin(), payload.end());
    const auto crc = crc32(out);
    appendLe<uint32_t>(out, crc);
    return out;
}

inline std::optional<Frame> decodeFrame(std::span<const std::byte> datagram,
                                        std::string &error) {
    if (datagram.size() < headerSize + crcSize) {
        error = "frame too small";
        return std::nullopt;
    }

    Header header{};
    if (!readHeader(datagram.first(headerSize), header)) {
        error = "header decode failed";
        return std::nullopt;
    }
    const auto expectedSize = headerSize + header.payloadSize + crcSize;
    if (expectedSize != datagram.size()) {
        error = "frame size mismatch";
        return std::nullopt;
    }

    size_t crcOffset = datagram.size() - crcSize;
    uint32_t expectedCrc = 0;
    if (!readLe<uint32_t>(datagram, crcOffset, expectedCrc)) {
        error = "crc decode failed";
        return std::nullopt;
    }
    const auto actualCrc = crc32(datagram.first(datagram.size() - crcSize));
    if (actualCrc != expectedCrc) {
        error = "crc mismatch";
        return std::nullopt;
    }

    return Frame{
        .header = header,
        .payload = datagram.subspan(headerSize, header.payloadSize),
    };
}

inline std::optional<PubSubEvent> decodePubSubEvent(
    std::span<const std::byte> payload) {
    if (payload.size() != 5) {
        return std::nullopt;
    }
    PubSubEvent event{};
    size_t offset = 0;
    if (!readLe<uint32_t>(payload, offset, event.topic) ||
        !readLe<uint8_t>(payload, offset, event.type)) {
        return std::nullopt;
    }
    return event;
}

inline std::optional<ButtonEvent> decodeButtonEvent(
    std::span<const std::byte> payload) {
    if (payload.size() != 2) {
        return std::nullopt;
    }
    ButtonEvent event{};
    size_t offset = 0;
    if (!readLe<uint8_t>(payload, offset, event.type) ||
        !readLe<uint8_t>(payload, offset, event.button)) {
        return std::nullopt;
    }
    return event;
}

inline std::string buttonTypeName(uint8_t value) {
    switch (static_cast<ButtonEventType>(value)) {
    case ButtonEventType::Pressed:
        return "Pressed";
    case ButtonEventType::Released:
        return "Released";
    }
    return "Unknown";
}

inline std::string buttonName(uint8_t value) {
    switch (static_cast<PeripheralButton>(value)) {
    case PeripheralButton::Bell:
        return "Bell";
    }
    return "Unknown";
}

inline std::string subscribeTypeName(uint8_t value) {
    switch (static_cast<SubscribeEventType>(value)) {
    case SubscribeEventType::Register:
        return "Register";
    case SubscribeEventType::Unregister:
        return "Unregister";
    }
    return "Unknown";
}

inline Header makeHostHeader(uint32_t messageId, uint32_t topic,
                             TrafficClass trafficClass) {
    return Header{
        .timestampMs = 0,
        .timestampUs = 0,
        .messageId = messageId,
        .topic = topic,
        .source = static_cast<uint8_t>(NodeId::Host),
        .trafficClass = static_cast<uint8_t>(trafficClass),
        .payloadSize = 0,
    };
}

inline Header makeHostHeader(uint32_t messageId, Topic topic,
                             TrafficClass trafficClass) {
    return makeHostHeader(messageId, static_cast<uint32_t>(topic),
                          trafficClass);
}

inline std::vector<std::byte> makeHostFrame(uint32_t messageId, uint32_t topic,
                                            TrafficClass trafficClass,
                                            std::span<const std::byte> payload) {
    return encodeFrame(makeHostHeader(messageId, topic, trafficClass), payload);
}

inline std::vector<std::byte>
makePubSubControlFrame(uint32_t messageId, Topic topic,
                       SubscribeEventType type) {
    const auto payload = encodePayload(PubSubEvent{
        .topic = static_cast<uint32_t>(topic),
        .type = static_cast<uint8_t>(type),
    });
    return encodeFrame(makeHostHeader(messageId, Topic::PubSub,
                                      TrafficClass::Critical),
                       payload);
}

inline std::vector<std::byte> makeSubscribeFrame(uint32_t messageId,
                                                 Topic topic) {
    return makePubSubControlFrame(messageId, topic,
                                  SubscribeEventType::Register);
}

} // namespace Totem::Tools::PubSubUdp
