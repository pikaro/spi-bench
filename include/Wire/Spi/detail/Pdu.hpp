#pragma once

#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include "Wire/Interfaces/Request.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>

#pragma push_macro("BIT_MASK")
#undef BIT_MASK
#include "inc/CRC.h"
#pragma pop_macro("BIT_MASK")

namespace Totem::Wire::Spi::detail {

using Totem::Wire::PayloadType;

enum class BucketSize : uint16_t {
    B64 = 64,
    B256 = 256,
    B512 = 512,
    B1024 = 1024,
    B4096 = 4096,
};

enum class FrameType : uint8_t {
    Nop = 0x00,
    Data = 0x01,
    Request = 0x02,
    Response = 0x03,
    Ack = 0x04,
    Nack = 0x05,
    Hello = 0x06,
    Heartbeat = 0x07,
    Status = 0x08,
};

enum class SlotFlags : uint16_t {
    None = 0,
    Hello = 1 << 0,
    Heartbeat = 1 << 1,
    Ack = 1 << 2,
    Nack = 1 << 3,
    Truncated = 1 << 4,
};

inline constexpr SlotFlags operator|(SlotFlags lhs, SlotFlags rhs) {
    return static_cast<SlotFlags>(static_cast<uint16_t>(lhs) |
                                  static_cast<uint16_t>(rhs));
}

inline constexpr bool hasFlag(SlotFlags flags, SlotFlags flag) {
    return (static_cast<uint16_t>(flags) & static_cast<uint16_t>(flag)) != 0;
}

enum class FrameFlags : uint8_t {
    None = 0,
    RequiresAck = 1 << 0,
    Critical = 1 << 1,
    AttentionSync = 1 << 2,
};

inline constexpr FrameFlags operator|(FrameFlags lhs, FrameFlags rhs) {
    return static_cast<FrameFlags>(static_cast<uint8_t>(lhs) |
                                   static_cast<uint8_t>(rhs));
}

inline constexpr bool hasFlag(FrameFlags flags, FrameFlags flag) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

inline constexpr uint16_t bucketBytes(BucketSize bucket) {
    return static_cast<uint16_t>(bucket);
}

inline constexpr BucketSize bucketFor(size_t bytes) {
    if (bytes <= bucketBytes(BucketSize::B64)) {
        return BucketSize::B64;
    }
    if (bytes <= bucketBytes(BucketSize::B256)) {
        return BucketSize::B256;
    }
    if (bytes <= bucketBytes(BucketSize::B512)) {
        return BucketSize::B512;
    }
    if (bytes <= bucketBytes(BucketSize::B1024)) {
        return BucketSize::B1024;
    }
    return BucketSize::B4096;
}

inline uint16_t readLe16(std::span<const std::byte> bytes, size_t offset) {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset])) |
        (static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset + 1]))
         << 8));
}

inline void writeLe16(std::span<std::byte> bytes, size_t offset,
                      uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::byte>((value >> 8) & 0xFF);
}

inline uint8_t crc8(std::span<const std::byte> bytes) {
    static const auto table = CRC::CRC_8().MakeTable();
    return CRC::Calculate(bytes.data(), bytes.size(), table);
}

struct SlotHeader {
    static constexpr uint8_t preamble = 0xA5;
    static constexpr uint8_t version = 1;
    static constexpr size_t checkedSize = 18;
    static constexpr size_t size = checkedSize + sizeof(uint8_t);

    uint16_t slotLength = 0;
    uint16_t bucketLength = 0;
    uint8_t peerId = 0;
    uint8_t connectionId = 0;
    uint16_t sequence = 0;
    uint16_t ackSequence = 0;
    SlotFlags flags = SlotFlags::None;
    uint8_t frameCount = 0;
    uint16_t payloadBytes = 0;
    uint8_t crc = 0;

    [[nodiscard]] std::array<std::byte, checkedSize> checkedBytes() const {
        std::array<std::byte, checkedSize> out{};
        out[0] = static_cast<std::byte>(preamble);
        out[1] = static_cast<std::byte>(version);
        out[2] = static_cast<std::byte>(size);
        writeLe16(out, 3, slotLength);
        writeLe16(out, 5, bucketLength);
        out[7] = static_cast<std::byte>(peerId);
        out[8] = static_cast<std::byte>(connectionId);
        writeLe16(out, 9, sequence);
        writeLe16(out, 11, ackSequence);
        writeLe16(out, 13, static_cast<uint16_t>(flags));
        out[15] = static_cast<std::byte>(frameCount);
        writeLe16(out, 16, payloadBytes);
        return out;
    }

    [[nodiscard]] std::array<std::byte, size> toBytes() const {
        std::array<std::byte, size> out{};
        auto checked = checkedBytes();
        std::copy(checked.begin(), checked.end(), out.begin());
        out[checkedSize] = static_cast<std::byte>(crc);
        return out;
    }

    void refreshCrc() { crc = crc8(checkedBytes()); }

    [[nodiscard]] ReturnCode validateCrc() const {
        if (crc8(checkedBytes()) != crc) {
            return ERR(WireError, CrcError);
        }
        return OK();
    }

    [[nodiscard]] ReturnCode validate() const {
        if (slotLength < size || slotLength > bucketLength ||
            payloadBytes > slotLength - size) {
            return ERR(WireError, Corrupted);
        }
        return validateCrc();
    }

    static std::expected<SlotHeader, ReturnCode>
    fromBytes(std::span<const std::byte> bytes) {
        if (bytes.size() < size) {
            return std::unexpected(ERR(WireError, Corrupted));
        }
        if (std::to_integer<uint8_t>(bytes[0]) != preamble) {
            return std::unexpected(ERR(WireError, Corrupted));
        }
        if (std::to_integer<uint8_t>(bytes[1]) != version) {
            return std::unexpected(ERR(WireError, Corrupted));
        }
        if (std::to_integer<uint8_t>(bytes[2]) != size) {
            return std::unexpected(ERR(WireError, Corrupted));
        }

        SlotHeader header{};
        header.slotLength = readLe16(bytes, 3);
        header.bucketLength = readLe16(bytes, 5);
        header.peerId = std::to_integer<uint8_t>(bytes[7]);
        header.connectionId = std::to_integer<uint8_t>(bytes[8]);
        header.sequence = readLe16(bytes, 9);
        header.ackSequence = readLe16(bytes, 11);
        header.flags = static_cast<SlotFlags>(readLe16(bytes, 13));
        header.frameCount = std::to_integer<uint8_t>(bytes[15]);
        header.payloadBytes = readLe16(bytes, 16);
        header.crc = std::to_integer<uint8_t>(bytes[checkedSize]);
        auto validateRet = header.validate();
        if (!validateRet.ok()) {
            return std::unexpected(validateRet);
        }
        return header;
    }
};

struct FrameHeader {
    static constexpr size_t checkedSize = 9;
    static constexpr size_t size = checkedSize + sizeof(uint8_t);

    FrameType type = FrameType::Nop;
    PayloadType payloadType = PayloadType::Raw;
    uint16_t sequence = 0;
    uint16_t responseTo = 0;
    uint16_t payloadLength = 0;
    FrameFlags flags = FrameFlags::None;
    uint8_t crc = 0;

    [[nodiscard]] std::array<std::byte, checkedSize> checkedBytes() const {
        std::array<std::byte, checkedSize> out{};
        out[0] = static_cast<std::byte>(type);
        out[1] = static_cast<std::byte>(payloadType);
        writeLe16(out, 2, sequence);
        writeLe16(out, 4, responseTo);
        writeLe16(out, 6, payloadLength);
        out[8] = static_cast<std::byte>(flags);
        return out;
    }

    [[nodiscard]] std::array<std::byte, size> toBytes() const {
        std::array<std::byte, size> out{};
        auto checked = checkedBytes();
        std::copy(checked.begin(), checked.end(), out.begin());
        out[checkedSize] = static_cast<std::byte>(crc);
        return out;
    }

    void refreshCrc() { crc = crc8(checkedBytes()); }

    [[nodiscard]] ReturnCode validateCrc() const {
        if (crc8(checkedBytes()) != crc) {
            return ERR(WireError, CrcError);
        }
        return OK();
    }

    static std::expected<FrameHeader, ReturnCode>
    fromBytes(std::span<const std::byte> bytes) {
        FAIL_IF(bytes.size() < size,
                std::unexpected(ERR(CoreError, InvalidArgument)),
                "SPI frame header buffer too small");
        FrameHeader header{};
        header.type = static_cast<FrameType>(std::to_integer<uint8_t>(bytes[0]));
        header.payloadType =
            static_cast<PayloadType>(std::to_integer<uint8_t>(bytes[1]));
        header.sequence = readLe16(bytes, 2);
        header.responseTo = readLe16(bytes, 4);
        header.payloadLength = readLe16(bytes, 6);
        header.flags = static_cast<FrameFlags>(std::to_integer<uint8_t>(bytes[8]));
        header.crc = std::to_integer<uint8_t>(bytes[9]);
        FAIL_IF_ERR_FWD_UNEXPECTED(header.validateCrc(),
                                   "Invalid SPI frame header");
        return header;
    }
};

struct FrameView {
    FrameHeader header{};
    std::span<const std::byte> payload{};
};

} // namespace Totem::Wire::Spi::detail
