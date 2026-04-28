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
static Sequence sequence{};

enum class FrameType : uint8_t {
    Data = 0x01,
    Hello = 0x02,
    Start = 0x03,
    Stop = 0x04,
    Ack = 0x07,
    Nack = 0x08,
};

static_assert(sizeof(FrameType) == 1, "FrameType must be 1 byte");
static_assert(sizeof(uint8_t) == 1, "uint8_t must be 1 byte");
static_assert(sizeof(uint16_t) == 2, "uint16_t must be 2 bytes");

struct Header {
    static constexpr uint8_t preamble = 0xAA;
    FrameType type;
    uint8_t sequenceNumber;
    uint16_t payloadLength;
    uint8_t crc8;

    static constexpr size_t _headerCheckedSize =
        sizeof(preamble) + sizeof(type) + sizeof(sequenceNumber) +
        sizeof(payloadLength);
    static constexpr size_t headerSize = _headerCheckedSize + sizeof(crc8);

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static Header make(FrameType type, uint16_t payloadLength) {
        Header frame{};
        frame.type = type;
        frame.sequenceNumber = sequence.next();
        frame.payloadLength = payloadLength;
        frame.crc8 = frame.crcSum();
        return frame;
    }

    static Header hello() { return make(FrameType::Hello, 0); }

    static Header start() { return make(FrameType::Start, 0); }

    static Header stop() { return make(FrameType::Stop, 0); }

    static std::expected<Header, ReturnCode> fromRequest(WriteRequest req) {
        FAIL_IF(!req.validate(),
                std::unexpected(ERR(CoreError, InvalidArgument)),
                "Invalid write request for frame");
        return make(FrameType::Data, static_cast<uint16_t>(req.data.size()));
    }

    [[nodiscard]] std::array<uint8_t, _headerCheckedSize>
    _checkedToBytes() const {
        return {
            preamble,
            static_cast<uint8_t>(type),
            sequenceNumber,
            static_cast<uint8_t>(payloadLength & 0xFF),
            static_cast<uint8_t>((payloadLength >> 8) & 0xFF),
        };
    }

    [[nodiscard]] std::array<uint8_t, headerSize> toBytes() const {
        return {
            preamble,
            static_cast<uint8_t>(type),
            sequenceNumber,
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
        FAIL_IF(header.preamble != bytes[0], std::unexpected(ERR(Corrupted)),
                "Invalid frame preamble: expected 0x%02X but got 0x%02X",
                header.preamble, bytes[0]);
        header.type = static_cast<FrameType>(bytes[1]);
        header.sequenceNumber = bytes[2];
        header.payloadLength =
            static_cast<uint16_t>(static_cast<uint16_t>(bytes[3]) |
                                  (static_cast<uint16_t>(bytes[4]) << 8));
        header.crc8 = bytes[5];
        FAIL_IF_ERR_FWD_UNEXPECTED(header.validateReceived(),
                                   "Failed to validate received frame header");
        return header;
    }

    [[nodiscard]] uint8_t crcSum() const {
        static const auto table = CRC::CRC_8().MakeTable();
        auto ints = _checkedToBytes();
        auto bytes = std::as_bytes(std::span(ints));
        return CRC::Calculate(bytes.data(), bytes.size(), table);
    }

    [[nodiscard]] ReturnCode validateReceived() const {
        if (preamble != 0xAA) {
            return ERR(Corrupted);
        }
        if (!sequence.received(sequenceNumber)) {
            return ERR(SequenceError);
        }
        if (crcSum() != crc8) {
            return ERR(CrcError);
        }
        return OK();
    }

    [[nodiscard]] constexpr std::optional<FrameType>
    expectedResponseType() const {
        switch (type) {
        case FrameType::Hello:
            return FrameType::Hello;
        case FrameType::Data:
            return FrameType::Ack;
        case FrameType::Start:
        case FrameType::Stop:
        case FrameType::Ack:
        case FrameType::Nack:
        default:
            return std::nullopt;
        }
    }
};

} // namespace Totem::Wire::Rs485::detail
