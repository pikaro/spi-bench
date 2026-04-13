#pragma once

#include "Macros/Facade.hh"
#include "PubSubBackend/Interfaces/Envelope.hh"
#include "PubSubBackend/Interfaces/Wire.hh"
#include "PubSubBackend/detail/Codec.hh"
#include "Types/Error.hh"
#ifdef BIT_MASK
#undef BIT_MASK
#endif
#include "inc/CRC.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <utility>

namespace Totem::PubSubBackend::detail {

class SerDe {
    using Reader = wire::Reader;

  public:
    static std::expected<size_t, ReturnCode>
    serialize(const Envelope &envelope, std::span<std::byte> out) {
        size_t totalSize =
            headerSize + envelope.header.payloadSize + overheadSize;
        FAIL_IF(out.size() < totalSize,
                std::unexpected(ERR(CoreError, Overflow)),
                "Output buffer is too small for serialized frame");

        auto headerSpan = out.subspan(0, headerSize);
        auto payloadSpan = out.subspan(headerSize, envelope.header.payloadSize);
        auto crcSpan = out.subspan(headerSize + envelope.header.payloadSize,
                                   sizeof(uint32_t));

        FAIL_IF_ERR_FWD_UNEXPECTED(
            Codec<Header>::encode(envelope.header, headerSpan),
            "Failed to encode header");

        if (envelope.getPayload != nullptr) {
            FAIL_IF_ERR_FWD_UNEXPECTED(
                envelope.getPayload(envelope.owner, envelope, 0, payloadSpan),
                "Failed to get payload for encoding");
        } else if (envelope.encodePayload != nullptr) {
            FAIL_IF_ERR_FWD_UNEXPECTED(
                envelope.encodePayload(envelope.owner, envelope, payloadSpan),
                "Failed to encode payload");
        } else {
            FAIL(std::unexpected(ERR(CoreError, InvalidState)),
                 "Envelope does not have a valid payload getter or encoder");
        }

        auto crc = _crcSum(out.first(headerSize + envelope.header.payloadSize));
        std::memcpy(crcSpan.data(), &crc, sizeof(crc));

        return totalSize;
    }

    static std::expected<std::pair<Header, std::span<const std::byte>>,
                         ReturnCode>
    deserializeRaw(std::span<const std::byte> frame) {
        FAIL_IF(frame.size() < headerSize + overheadSize,
                std::unexpected(ERR(CoreError, InvalidData)),
                "Frame size is too small to contain header and CRC");

        auto crcSpan =
            frame.subspan(frame.size() - sizeof(uint32_t), sizeof(uint32_t));
        uint32_t expectedCrc{};
        std::memcpy(&expectedCrc, crcSpan.data(), sizeof(expectedCrc));

        auto actualCrc = _crcSum(frame.first(frame.size() - sizeof(uint32_t)));
        if (actualCrc != expectedCrc) {
            return std::unexpected(ERR(CoreError, InvalidData));
        }

        auto headerSpan = frame.first(headerSize);
        auto payloadSpan =
            frame.subspan(headerSize, frame.size() - overheadSize - headerSize);

        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(header,
                                          Codec<Header>::decode(headerSpan),
                                          "Failed to decode header from frame");
        FAIL_IF(payloadSpan.size() != header.payloadSize,
                std::unexpected(ERR(CoreError, InvalidData)),
                "Payload size in frame does not match payloadSize in header");
        return std::make_pair(header, payloadSpan);
    }

    template <typename T>
    static std::expected<std::pair<Header, T>, ReturnCode>
    deserialize(std::span<const std::byte> frame) {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            data, deserializeRaw(frame),
            "Failed to deserialize frame: failed to parse header and payload");
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            payload, Codec<T>::decode(data.second),
            "Failed to decode payload from frame");
        return std::make_pair(std::move(data.first), std::move(payload));
    }

    template <typename T> [[nodiscard]] static constexpr size_t encodedSize() {
        return headerSize + Codec<T>::encodedSize() + overheadSize;
    }

    [[nodiscard]] static constexpr size_t
    encodedSize(const Envelope &envelope) {
        return encodedSize(envelope.header);
    }

    [[nodiscard]] static constexpr size_t encodedSize(const Header &header) {
        return headerSize + overheadSize + header.payloadSize;
    }

    static constexpr size_t headerSize = Codec<Header>::encodedSize();
    static constexpr size_t overheadSize = sizeof(uint32_t);

  private:
    [[nodiscard]] static uint32_t
    _crcSum(const std::span<const std::byte> frame) {
        return CRC::Calculate(frame.data(), frame.size(), CRC::CRC_32());
    }
};

} // namespace Totem::PubSubBackend::detail
