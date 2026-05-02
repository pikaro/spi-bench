#pragma once

#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Wire.hpp"
#include "PubSubBackend/detail/Codec.hpp"
#include "PubSubBackend/detail/Trace.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Platform/Crc/Facade.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <type_traits>
#include <utility>

namespace Totem::PubSubBackend::detail {

class SerDe {
    using Reader = wire::Reader;
    using Topic = typename Spec::Topic;
    using NodeId = typename Spec::NodeId;

  public:
    static std::expected<size_t, ReturnCode>
    serialize(const Envelope &envelope, std::span<std::byte> out) {
        size_t totalSize =
            headerSize + envelope.header.payloadSize + overheadSize;
        log_trace_packet("serde.serialize.begin", envelope.header);
        _log_d("SerDe: serialize " MAGIC_PUBSUB_SV_FMT " into %zu bytes",
               MAGIC_PUBSUB_SV_ARG(envelope.header), totalSize);
        FAIL_IF(out.size() < totalSize,
                std::unexpected(ERR(CoreError, Overflow)),
                "Output buffer is too small for serialized frame");

        auto headerSpan = out.subspan(0, headerSize);
        auto payloadSpan = out.subspan(headerSize, envelope.header.payloadSize);
        auto crcSpan = out.subspan(headerSize + envelope.header.payloadSize,
                                   sizeof(uint32_t));

        FAIL_IF_ERR_FWD_UNEXPECTED(encodeHeader(envelope.header, headerSpan),
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

        log_trace_packet("serde.serialize.done", envelope.header);
        return totalSize;
    }

    static std::expected<std::pair<Header, std::span<const std::byte>>,
                         ReturnCode>
    deserializeRaw(std::span<const std::byte> frame) {
        _log_d("SerDe: deserialize raw frame of %zu bytes", frame.size());
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            header, peekHeader(frame), "Failed to peek header from frame");
        log_trace_packet("serde.deserialize.peek", header);
        FAIL_IF_ERR_FWD_UNEXPECTED(validateFrame(frame, header),
                                   "Failed to validate frame for "
                                   MAGIC_PUBSUB_SV_FMT,
                                   MAGIC_PUBSUB_SV_ARG(header));
        auto payloadSpan = frame.subspan(headerSize, header.payloadSize);
        _log_d("SerDe: deserialized " MAGIC_PUBSUB_SV_FMT,
               MAGIC_PUBSUB_SV_ARG(header));
        log_trace_packet("serde.deserialize.done", header);
        return std::make_pair(header, payloadSpan);
    }

    static std::expected<Header, ReturnCode>
    peekHeader(std::span<const std::byte> frame) {
        FAIL_IF(frame.size() < headerSize + overheadSize,
                std::unexpected(ERR(CoreError, InvalidData)),
                "Frame size is too small to contain header and CRC");
        auto headerSpan = frame.first(headerSize);
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(header, decodeHeader(headerSpan),
                                          "Failed to decode header from frame");
        log_trace_packet("serde.peekHeader", header);
        FAIL_IF(encodedSize(header) != frame.size(),
                std::unexpected(ERR(CoreError, InvalidData)),
                "Frame size does not match encoded size for "
                MAGIC_PUBSUB_SV_FMT,
                MAGIC_PUBSUB_SV_ARG(header));
        return header;
    }

    static std::expected<Header, ReturnCode>
    tryPeekHeader(std::span<const std::byte> frame) {
        if (frame.size() < headerSize + overheadSize) {
            return std::unexpected(ERR(CoreError, InvalidData));
        }
        auto headerResult = decodeHeader(frame.first(headerSize));
        if (!headerResult) {
            return std::unexpected(headerResult.error());
        }
        auto header = *headerResult;
        if (encodedSize(header) != frame.size()) {
            return std::unexpected(ERR(CoreError, InvalidData));
        }
        return header;
    }

    static ReturnCode validateFrame(std::span<const std::byte> frame,
                                    const Header &header) {
        FAIL_IF(encodedSize(header) != frame.size(), ERR(CoreError, InvalidData),
                "Frame size does not match encoded size for "
                MAGIC_PUBSUB_SV_FMT,
                MAGIC_PUBSUB_SV_ARG(header));
        auto crcSpan =
            frame.subspan(frame.size() - sizeof(uint32_t), sizeof(uint32_t));
        uint32_t expectedCrc{};
        std::memcpy(&expectedCrc, crcSpan.data(), sizeof(expectedCrc));

        auto actualCrc = _crcSum(frame.first(frame.size() - sizeof(uint32_t)));
        FAIL_IF(actualCrc != expectedCrc, ERR(CoreError, InvalidData),
                "CRC mismatch for " MAGIC_PUBSUB_SV_FMT,
                MAGIC_PUBSUB_SV_ARG(header));
        log_trace_packet("serde.validate", header);
        return OK();
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

    static constexpr size_t headerSize = sizeof(uint32_t) + sizeof(uint64_t) +
                                         sizeof(MessageId) + sizeof(TopicId) +
                                         sizeof(NodeId) +
                                         sizeof(TrafficClass) +
                                         sizeof(uint16_t);
    static constexpr size_t overheadSize = sizeof(uint32_t);

  private:
    static_assert(headerSize == Codec<Header>::encodedSize(),
                  "Manual PubSub header codec must match generated codec");

    template <typename T, bool IsEnum = std::is_enum_v<T>>
    struct HeaderScalarStorage {
        using Type = T;
    };

    template <typename T> struct HeaderScalarStorage<T, true> {
        using Type = std::underlying_type_t<T>;
    };

    template <typename T>
    static void _writeLe(std::span<std::byte> out, size_t &offset, T value) {
        using RawT = typename HeaderScalarStorage<T>::Type;
        using UnsignedT = std::make_unsigned_t<RawT>;
        auto bits = static_cast<UnsignedT>(static_cast<RawT>(value));
        for (size_t i = 0; i < sizeof(RawT); ++i) {
            out[offset + i] = static_cast<std::byte>((bits >> (i * 8U)) &
                                                     0xFFU);
        }
        offset += sizeof(RawT);
    }

    template <typename T>
    static T _readLe(std::span<const std::byte> in, size_t &offset) {
        using RawT = typename HeaderScalarStorage<T>::Type;
        using UnsignedT = std::make_unsigned_t<RawT>;
        UnsignedT bits{};
        for (size_t i = 0; i < sizeof(RawT); ++i) {
            bits |= static_cast<UnsignedT>(std::to_integer<uint8_t>(
                        in[offset + i]))
                    << (i * 8U);
        }
        offset += sizeof(RawT);
        return static_cast<T>(static_cast<RawT>(bits));
    }

    static ReturnCode encodeHeader(const Header &header,
                                   std::span<std::byte> out) {
        FAIL_IF(out.size() < headerSize, ERR(CoreError, Overflow),
                "Output buffer is too small for PubSub header");
        size_t offset = 0;
        _writeLe(out, offset, header.timestampMs);
        _writeLe(out, offset, header.timestampUs);
        _writeLe(out, offset, header.messageId);
        _writeLe(out, offset, header.topic);
        _writeLe(out, offset, header.source);
        _writeLe(out, offset, header.trafficClass);
        _writeLe(out, offset, header.payloadSize);
        return OK();
    }

    static std::expected<Header, ReturnCode>
    decodeHeader(std::span<const std::byte> in) {
        FAIL_IF(in.size() < headerSize,
                std::unexpected(ERR(CoreError, Underflow)),
                "Input buffer is too small for PubSub header");
        size_t offset = 0;
        return Header{
            .timestampMs = _readLe<uint32_t>(in, offset),
            .timestampUs = _readLe<uint64_t>(in, offset),
            .messageId = _readLe<MessageId>(in, offset),
            .topic = _readLe<TopicId>(in, offset),
            .source = _readLe<::Totem::PubSubBackend::NodeId>(in, offset),
            .trafficClass = _readLe<TrafficClass>(in, offset),
            .payloadSize = _readLe<uint16_t>(in, offset),
        };
    }

    [[nodiscard]] static uint32_t
    _crcSum(const std::span<const std::byte> frame) {
        return Totem::Platform::Crc::Platform::crc32(frame);
    }
};

} // namespace Totem::PubSubBackend::detail
