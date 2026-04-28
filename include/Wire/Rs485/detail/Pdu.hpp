#pragma once

#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include "Wire/Interfaces/Request.hpp"
#include "Wire/Rs485/detail/Types.hpp" // IWYU pragma: keep
#include "Wire/detail/Sequence.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

#pragma push_macro("BIT_MASK")
#undef BIT_MASK
#include "inc/CRC.h"
#pragma pop_macro("BIT_MASK")

namespace Totem::Wire::Rs485::detail {

using Totem::Wire::detail::Sequence;
inline Sequence sequence{};

enum class FrameType : uint8_t {
    Nop = 0x00,
    Data = 0x01,
    Hello = 0x02,
    Request = 0x03,
    Response = 0x04,
    Poll = 0x05,
    Heartbeat = 0x06,
    Ack = 0x07,
    Nack = 0x08,
};

static_assert(sizeof(FrameType) == 1, "FrameType must be 1 byte");
static_assert(sizeof(uint8_t) == 1, "uint8_t must be 1 byte");
static_assert(sizeof(uint16_t) == 2, "uint16_t must be 2 bytes");

struct Header {
    static constexpr uint8_t preamble = 0xAA;
    FrameType type;
    PayloadType payloadType;
    uint8_t sequenceNumber;
    uint8_t responseTo;
    uint16_t payloadLength;
    uint8_t crc8;

    static constexpr size_t _headerCheckedSize =
        sizeof(preamble) + sizeof(type) + sizeof(payloadType) +
        sizeof(sequenceNumber) + sizeof(responseTo) + sizeof(payloadLength);
    static constexpr size_t headerSize = _headerCheckedSize + sizeof(crc8);

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static Header make(FrameType type, PayloadType payloadType,
                       uint16_t payloadLength, uint8_t responseTo = 0) {
        Header frame{};
        frame.type = type;
        frame.payloadType = payloadType;
        frame.sequenceNumber = sequence.next();
        frame.responseTo = responseTo;
        frame.payloadLength = payloadLength;
        frame.crc8 = frame.crcSum();
        return frame;
    }

    static Header hello(uint8_t responseTo = 0) {
        return make(FrameType::Hello, PayloadType::Raw, 0, responseTo);
    }

    static Header ack(const Header &received) {
        return make(FrameType::Ack, received.payloadType, 0,
                    received.sequenceNumber);
    }

    static Header nack(const Header &received) {
        return make(FrameType::Nack, received.payloadType, 0,
                    received.sequenceNumber);
    }

    [[nodiscard]] std::array<uint8_t, _headerCheckedSize>
    _checkedToBytes() const {
        return {
            preamble,
            static_cast<uint8_t>(type),
            static_cast<uint8_t>(payloadType),
            sequenceNumber,
            responseTo,
            static_cast<uint8_t>(payloadLength & 0xFF),
            static_cast<uint8_t>((payloadLength >> 8) & 0xFF),
        };
    }

    [[nodiscard]] std::array<uint8_t, headerSize> toBytes() const {
        return {
            preamble,
            static_cast<uint8_t>(type),
            static_cast<uint8_t>(payloadType),
            sequenceNumber,
            responseTo,
            static_cast<uint8_t>(payloadLength & 0xFF),
            static_cast<uint8_t>((payloadLength >> 8) & 0xFF),
            crc8,
        };
    }

    [[nodiscard]] static std::expected<Header, ReturnCode>
    fromBytes(std::span<const uint8_t> bytes) {
        if (bytes.size() != headerSize) {
            return std::unexpected(ERR(CoreError, InvalidArgument));
        }
        Header header{};
        FAIL_IF(Header::preamble != bytes[0], std::unexpected(ERR(Corrupted)),
                "Invalid frame preamble: expected 0x%02X but got 0x%02X",
                Header::preamble, bytes[0]);
        header.type = static_cast<FrameType>(bytes[1]);
        header.payloadType = static_cast<PayloadType>(bytes[2]);
        header.sequenceNumber = bytes[3];
        header.responseTo = bytes[4];
        header.payloadLength =
            static_cast<uint16_t>(static_cast<uint16_t>(bytes[5]) |
                                  (static_cast<uint16_t>(bytes[6]) << 8));
        header.crc8 = bytes[7];
        FAIL_IF_ERR_FWD_UNEXPECTED(header.validateCrc(),
                                   "Failed to validate received frame header");
        return header;
    }

    [[nodiscard]] uint8_t crcSum() const {
        static const auto table = CRC::CRC_8().MakeTable();
        auto ints = _checkedToBytes();
        auto bytes = std::as_bytes(std::span(ints));
        return CRC::Calculate(bytes.data(), bytes.size(), table);
    }

    [[nodiscard]] ReturnCode validateCrc() const {
        if (preamble != 0xAA) {
            return ERR(Corrupted);
        }
        if (crcSum() != crc8) {
            return ERR(CrcError);
        }
        return OK();
    }

    [[nodiscard]] ReturnCode validateSequence() const {
        if (!sequence.received(sequenceNumber)) {
            return ERR(SequenceError);
        }
        return OK();
    }
};

} // namespace Totem::Wire::Rs485::detail
