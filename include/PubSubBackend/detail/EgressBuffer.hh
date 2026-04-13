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
#include <span>

namespace Totem::PubSubBackend::detail {

template <class Config>
class EgressBuffer : public ByteArena<EgressBuffer<Config>, Header, Config> {
    using Base = ByteArena<EgressBuffer<Config>, Header, Config>;
    using Topic = typename Spec::Topic;
    using NodeId = typename Spec::NodeId;

  public:
    static constexpr const char *name = "PubSub::EgressBuffer";

    ReturnCode store(const Header &header, std::span<const std::byte> frame) {
        FAIL_IF(frame.size() != SerDe::encodedSize(header),
                ERR(InvalidArgument), "Payload size does not match frame size");
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

    ReturnCode release(const Header &header) { return Base::release(header); }

  private:
    using DefaultError = CoreError;
};

} // namespace Totem::PubSubBackend::detail
