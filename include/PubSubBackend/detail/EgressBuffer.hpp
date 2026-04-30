#pragma once

#include "Generic/ByteArena.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/detail/SerDe.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <span>

namespace Totem::PubSubBackend::detail {

template <class Config>
class EgressBuffer : public ByteArena<EgressBuffer<Config>, Header, Config> {
    using Base = ByteArena<EgressBuffer<Config>, Header, Config>;
    using Topic = typename Spec::Topic;
    using NodeId = typename Spec::NodeId;

  public:
    static constexpr LogComponent logComponent =
        Totem::PubSubBackend::detail::logComponent;

    EgressBuffer() = default;

    static constexpr const char *name = "PubSub::EgressBuffer";

    ReturnCode store(const Header &header, std::span<const std::byte> frame) {
        FAIL_IF(frame.size() != SerDe::encodedSize(header),
                ERR(InvalidArgument), "Payload size does not match frame size");
        _log_d("%s: store frame of %zu bytes for " MAGIC_PUBSUB_SV_FMT, name,
               frame.size(), MAGIC_PUBSUB_SV_ARG(header));
        return Base::store(header, frame);
    }

    static ReturnCode getRaw(void *egressBuffer, const Header &header,
                             std::span<std::byte> out) {
        auto *self = static_cast<EgressBuffer *>(egressBuffer);
        return self->getRaw(header, out);
    }

    ReturnCode getRaw(const Header &header, std::span<std::byte> out) const {
        FAIL_IF(out.size() > SerDe::encodedSize(header), ERR(InvalidArgument),
                "Requested range exceeds frame size for " MAGIC_PUBSUB_SV_FMT,
                MAGIC_PUBSUB_SV_ARG(header));
        return Base::getRaw(header, 0, out);
    }

    static ReturnCode release(void *egressBuffer, const Header &header) {
        auto *self = static_cast<EgressBuffer *>(egressBuffer);
        return self->release(header);
    }

    ReturnCode release(const Header &header) {
        _log_d("%s: release " MAGIC_PUBSUB_SV_FMT, name,
               MAGIC_PUBSUB_SV_ARG(header));
        return Base::release(header);
    }

    /**
     * Check whether a serialized frame is still retained in transport-owned
     * egress storage.
     */
    [[nodiscard]] bool contains(const Header &header) const {
        return Base::contains(header);
    }

    [[nodiscard]] bool wasFreed(const Header &header) const {
        return !contains(header);
    }
};

} // namespace Totem::PubSubBackend::detail
