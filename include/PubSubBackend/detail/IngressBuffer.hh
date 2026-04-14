#pragma once

#include "Generic/ByteArena.hh"
#include "Macros/Facade.hh"
#include "PubSubBackend/Interfaces/Envelope.hh"
#include "PubSubBackend/detail/SerDe.hh"
#include "PubSubBackend/detail/Types.hh"
#include "Types/Error.hh"
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <expected>
#include <span>

namespace Totem::PubSubBackend::detail {

struct IngressByteArenaConfig {
    static constexpr size_t bufferSize = Spec::Limits::ingressBufferSize;
    static constexpr size_t slotCount = Spec::Limits::maxIngressRecords;
    static constexpr size_t spanCount = Spec::Limits::maxIngressSpans;
    static constexpr size_t maxRecordAgeMs =
        Spec::Limits::maxIngressRecordAgeMs;
};

class IngressBuffer
    : public ByteArena<IngressBuffer, Header, IngressByteArenaConfig> {
    using Topic = typename Spec::Topic;
    using NodeId = typename Spec::NodeId;

  public:
    static constexpr const char *name = "PubSub::IngressBuffer";

    std::expected<Envelope, ReturnCode>
    storeFrame(std::span<const std::byte> frame) {
        _log_d("%s: storing raw frame of %zu bytes", name, frame.size());
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            data, SerDe::deserializeRaw(frame),
            "Failed to deserialize frame for storage");
        FAIL_IF_ERR_FWD_UNEXPECTED(
            store(data.first, data.second),
            "Failed to store deserialized frame for " MAGIC_PUBSUB_SV_FMT,
            MAGIC_PUBSUB_SV_ARG(data.first));
        return Envelope{
            .header = data.first,
            .owner = this,
            .getPayload = getRaw,
            .release = release,
        };
    }

    ReturnCode store(const Envelope &envelope,
                     std::span<const std::byte> payload) {
        return store(envelope.header, payload);
    }

    ReturnCode store(const Header &header, std::span<const std::byte> payload) {
        FAIL_IF(payload.size() != header.payloadSize, ERR(InvalidArgument),
                "Payload size does not match size in header");
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
        return ByteArena::getRaw(header, offset, out);
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
        return ByteArena::release(envelope.header);
    }

    ReturnCode release(const Header &header) {
        _log_d("%s: release " MAGIC_PUBSUB_SV_FMT, name,
               MAGIC_PUBSUB_SV_ARG(header));
        return ByteArena::release(header);
    }
};

} // namespace Totem::PubSubBackend::detail
