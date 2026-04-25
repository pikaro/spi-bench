#pragma once

#include "LocalTransport.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "PubSubBackend/Interfaces/Wire.hpp"
#include "PubSubBackend/detail/EgressBuffer.hpp"
#include "PubSubBackend/detail/Metrics.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <span>

namespace Totem::PubSubBackend::Transports {

struct LocalTransportEgressByteArenaConfig {
    static constexpr size_t bufferSize = 512;
    static constexpr size_t slotCount = 32;
    static constexpr size_t spanCount = 32;
    static constexpr size_t maxRecordAgeMs = 1000;

    [[nodiscard]] static bool isCritical(const Header &header) {
        return header.trafficClass == TrafficClass::Critical;
    }

    static ReturnCode onEvictNoncritical(const Header &) {
        return detail::metrics().addEgressEvictedNoncritical();
    }

    static ReturnCode onDropNoncritical(const Header &) {
        return detail::metrics().addEgressDroppedNoncritical();
    }

    static ReturnCode onRejectCritical(const Header &) {
        return detail::metrics().addEgressRejectedCritical();
    }
};

class LocalBufferedTransport : public LocalTransport {
    using NodeId = typename detail::Spec::NodeId;
    using Topic = typename detail::Spec::Topic;

  public:
    explicit LocalBufferedTransport(LocalTransportDependencies deps)
        : LocalTransport({.base = deps.withBaseDeps(this, _sendCallback)}) {}

    static constexpr const char *name = "LocalBufferedTransport";

    ReturnCode ackSend(const Envelope &envelope) {
        return ackSend(envelope.header);
    }

    ReturnCode ackSend(const Header &header) {
        _log_d("%s: external ack for " MAGIC_PUBSUB_SV_FMT, name,
               MAGIC_PUBSUB_SV_ARG(header));
        FAIL_IF_ERR(_egressBuffer.release(header), ERR(OperationFailed),
                    "Failed to release frame from egress buffer for "
                    "LocalBufferedTransport for " MAGIC_PUBSUB_SV_FMT,
                    MAGIC_PUBSUB_SV_ARG(header));
        return OK();
    }

  private:
    static ReturnCode _sendCallback(void *localBufferedTransport,
                                    const Header &header,
                                    std::span<const std::byte> frame) {
        auto *self =
            static_cast<LocalBufferedTransport *>(localBufferedTransport);
        return self->_send(header, frame);
    }

    ReturnCode _send(const Header &header, std::span<const std::byte> frame) {
        _log_d("%s: buffered send of %zu bytes for " MAGIC_PUBSUB_SV_FMT, name,
               frame.size(), MAGIC_PUBSUB_SV_ARG(header));
        FAIL_IF_ERR_FWD(LocalTransport::_send(header, frame),
                        "Failed to send frame over LocalTransport");
        FAIL_IF_UNEXPECTED_FWD(storeRet, _egressBuffer.store(header, frame),
                               "Failed to store frame in egress buffer for "
                               "LocalBufferedTransport");
        if (!storeRet) {
            _log_w("%s: dropped noncritical buffered frame with message ID %lu "
                   "under egress pressure",
                   name, static_cast<unsigned long>(header.messageId));
        }
        return OK();
    }

    detail::EgressBuffer<LocalTransportEgressByteArenaConfig> _egressBuffer;
};

} // namespace Totem::PubSubBackend::Transports
