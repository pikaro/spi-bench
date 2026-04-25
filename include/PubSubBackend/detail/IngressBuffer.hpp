#pragma once

#include "Generic/ByteArena.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "PubSubBackend/detail/Metrics.hpp"
#include "PubSubBackend/detail/SerDe.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <expected>
#include <optional>
#include <span>

namespace Totem::PubSubBackend::detail {

struct IngressByteArenaConfig {
    static constexpr size_t bufferSize = Spec::Limits::ingressBufferSize;
    static constexpr size_t slotCount = Spec::Limits::maxIngressRecords;
    static constexpr size_t spanCount = Spec::Limits::maxIngressSpans;
    static constexpr size_t maxRecordAgeMs =
        Spec::Limits::maxIngressRecordAgeMs;

    [[nodiscard]] static bool isCritical(const Header &header) {
        return header.trafficClass == TrafficClass::Critical;
    }

    static ReturnCode onEvictNoncritical(const Header & /*unused*/) {
        return metrics().addIngressEvictedNoncritical();
    }

    static ReturnCode onDropNoncritical(const Header & /*unused*/) {
        return metrics().addIngressDroppedNoncritical();
    }

    static ReturnCode onRejectCritical(const Header & /*unused*/) {
        return metrics().addIngressRejectedCritical();
    }
};

class IngressBuffer;

using ByteArenaImpl = ByteArena<IngressBuffer, Header, IngressByteArenaConfig>;

class IngressBuffer : public ByteArenaImpl {
    using Topic = typename Spec::Topic;
    using NodeId = typename Spec::NodeId;
    struct SerializedRecord {
        Header header{};
        bool occupied = false;
        bool validated = false;
    };

  public:
    IngressBuffer()
        : ByteArenaImpl(Totem::PubSubBackend::detail::logComponent) {}

    static constexpr const char *name = "PubSub::IngressBuffer";

    std::expected<std::optional<Envelope>, ReturnCode>
    storeFrame(std::span<const std::byte> frame) {
        _log_d("%s: storing raw frame of %zu bytes", name, frame.size());
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            header, SerDe::peekHeader(frame),
            "Failed to peek frame header for storage");
        if (!_canRememberSerialized(header)) {
            FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
                data, SerDe::deserializeRaw(frame),
                "Failed to deserialize frame for payload-only storage");
            FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
                stored, store(data.first, data.second),
                "Failed to store deserialized frame for "
                MAGIC_PUBSUB_SV_FMT,
                MAGIC_PUBSUB_SV_ARG(data.first));
            if (!stored) {
                return std::optional<Envelope>{};
            }
            return std::optional<Envelope>{Envelope{
                .header = data.first,
                .owner = this,
                .getPayload = getRaw,
                .release = release,
            }};
        }
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            stored, ByteArena::store(header, frame),
            "Failed to store serialized frame for " MAGIC_PUBSUB_SV_FMT,
            MAGIC_PUBSUB_SV_ARG(header));
        if (!stored) {
            return std::optional<Envelope>{};
        }
        _rememberSerialized(header);
        return std::optional<Envelope>{Envelope{
            .header = header,
            .owner = this,
            .getPayload = getRaw,
            .release = release,
        }};
    }

    std::expected<bool, ReturnCode> store(const Envelope &envelope,
                                          std::span<const std::byte> payload) {
        return store(envelope.header, payload);
    }

    std::expected<bool, ReturnCode> store(const Header &header,
                                          std::span<const std::byte> payload) {
        FAIL_IF(payload.size() != header.payloadSize,
                std::unexpected(ERR(InvalidArgument)),
                "Payload size does not match size in header");
        _forgetSerialized(header);
        _log_d("%s: store payload of %zu bytes for " MAGIC_PUBSUB_SV_FMT, name,
               payload.size(), MAGIC_PUBSUB_SV_ARG(header));
        return ByteArena::store(header, payload);
    }

    static ReturnCode getRaw(void *ingressBuffer, const Envelope &envelope,
                             size_t offset, std::span<std::byte> out) {
        auto *self = static_cast<IngressBuffer *>(ingressBuffer);
        return self->getRaw(envelope.header, offset, out);
    }

    static ReturnCode getRaw(void *ingressBuffer, const Header &header,
                             size_t offset, std::span<std::byte> out) {
        auto *self = static_cast<IngressBuffer *>(ingressBuffer);
        return self->getRaw(header, offset, out);
    }

    ReturnCode getRaw(const Envelope &envelope, size_t offset,
                      std::span<std::byte> out) const {
        return getRaw(envelope.header, offset, out);
    }

    ReturnCode getRaw(const Header &header, size_t offset,
                      std::span<std::byte> out) const {
        FAIL_IF(offset > header.payloadSize, ERR(InvalidArgument),
                "Offset exceeds payload size for " MAGIC_PUBSUB_SV_FMT,
                MAGIC_PUBSUB_SV_ARG(header));
        FAIL_IF(offset + out.size() > header.payloadSize, ERR(InvalidArgument),
                "Requested range exceeds payload size for " MAGIC_PUBSUB_SV_FMT,
                MAGIC_PUBSUB_SV_ARG(header));
        if (hasSerializedFrame(header)) {
            FAIL_IF_ERR_FWD(_ensureSerializedValidated(header),
                            "Failed to validate serialized ingress frame for "
                            MAGIC_PUBSUB_SV_FMT,
                            MAGIC_PUBSUB_SV_ARG(header));
            return ByteArena::getRaw(header, SerDe::headerSize + offset, out);
        }
        return ByteArena::getRaw(header, offset, out);
    }

    [[nodiscard]] bool hasSerializedFrame(const Header &header) const {
        return _serializedRecordIndex(header).has_value();
    }

    ReturnCode getSerializedFrame(const Header &header,
                                  std::span<std::byte> out) const {
        const auto frameSize = SerDe::encodedSize(header);
        FAIL_IF(out.size() < frameSize, ERR(InvalidArgument),
                "Serialized frame buffer too small for "
                MAGIC_PUBSUB_SV_FMT,
                MAGIC_PUBSUB_SV_ARG(header));
        FAIL_IF_NOT(hasSerializedFrame(header), ERR(NotFound),
                    "Serialized frame not retained for "
                    MAGIC_PUBSUB_SV_FMT,
                    MAGIC_PUBSUB_SV_ARG(header));
        return ByteArena::getRaw(header, 0, out.first(frameSize));
    }

    static ReturnCode release(void *ingressBuffer, const Envelope &envelope) {
        auto *self = static_cast<IngressBuffer *>(ingressBuffer);
        return self->release(envelope.header);
    }

    static ReturnCode release(void *ingressBuffer, const Header &header) {
        auto *self = static_cast<IngressBuffer *>(ingressBuffer);
        return self->release(header);
    }

    ReturnCode release(const Envelope &envelope) {
        _log_d("%s: release " MAGIC_PUBSUB_SV_FMT, name,
               MAGIC_PUBSUB_SV_ARG(envelope.header));
        return release(envelope.header);
    }

    ReturnCode release(const Header &header) {
        _log_d("%s: release " MAGIC_PUBSUB_SV_FMT, name,
               MAGIC_PUBSUB_SV_ARG(header));
        _forgetSerialized(header);
        return ByteArena::release(header);
    }

  private:
    [[nodiscard]] std::optional<size_t>
    _serializedRecordIndex(const Header &header) const {
        for (size_t i = 0; i < _serializedRecords.size(); ++i) {
            if (_serializedRecords[i].occupied &&
                _serializedRecords[i].header == header) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool _canRememberSerialized(const Header &header) const {
        return _serializedRecordIndex(header).has_value() ||
               std::ranges::any_of(_serializedRecords, [](const auto &record) {
                   return !record.occupied;
               });
    }

    void _rememberSerialized(const Header &header) {
        if (auto existing = _serializedRecordIndex(header); existing) {
            return;
        }
        for (auto &record : _serializedRecords) {
            if (record.occupied) {
                continue;
            }
            record.header = header;
            record.occupied = true;
            record.validated = false;
            return;
        }
        _log_d("%s: serialized ingress metadata full for "
               MAGIC_PUBSUB_SV_FMT,
               name, MAGIC_PUBSUB_SV_ARG(header));
    }

    void _forgetSerialized(const Header &header) {
        if (auto existing = _serializedRecordIndex(header); existing) {
            _serializedRecords[*existing] = SerializedRecord{};
        }
    }

    ReturnCode _ensureSerializedValidated(const Header &header) const {
        auto recordIndex = _serializedRecordIndex(header);
        FAIL_IF_NOT(recordIndex.has_value(), ERR(NotFound),
                    "Serialized ingress metadata missing for "
                    MAGIC_PUBSUB_SV_FMT,
                    MAGIC_PUBSUB_SV_ARG(header));
        auto &record = _serializedRecords[*recordIndex];
        if (record.validated) {
            return OK();
        }
        auto frameBuffer = std::array<std::byte, SerDe::headerSize +
                                                 Spec::Limits::maxPayloadSize +
                                                 SerDe::overheadSize>{};
        auto frameSize = SerDe::encodedSize(header);
        auto frameSpan = std::span<std::byte>{frameBuffer.data(), frameSize};
        FAIL_IF_ERR_FWD(ByteArena::getRaw(header, 0, frameSpan),
                        "Failed to read serialized ingress frame for "
                        "validation");
        FAIL_IF_ERR_FWD(SerDe::validateFrame(frameSpan, header),
                        "Failed to validate serialized ingress frame bytes");
        record.validated = true;
        return OK();
    }

    mutable std::array<SerializedRecord, Spec::Limits::maxMessageQueueSize>
        _serializedRecords{};
};

} // namespace Totem::PubSubBackend::detail
