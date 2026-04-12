#pragma once

#include "LocalTransport.hh"
#include "Macros/Facade.hh"
#include "PubSubBackend/Interfaces/Envelope.hh"
#include "PubSubBackend/Interfaces/Wire.hh"
#include "PubSubBackend/detail/EgressBuffer.hh"
#include "PubSubBackend/detail/Types.hh"
#include "Types/Error.hh"
#include <cstddef>
#include <span>

namespace Totem::PubSubBackend::Transports {

struct LocalTransportEgressByteArenaConfig {
    static constexpr size_t bufferSize = 512;
    static constexpr size_t slotCount = 32;
    static constexpr size_t spanCount = 32;
    static constexpr size_t maxRecordAgeMs = 1000;
};

class LocalBufferedTransport : public LocalTransport {
    using NodeId = typename detail::Spec::NodeId;
    using Topic = typename detail::Spec::Topic;

  public:
    explicit LocalBufferedTransport(LocalTransportDependencies deps)
        : LocalTransport(deps) {}
    explicit LocalBufferedTransport(LocalTransportDependencies deps)
        : LocalTransport({.base = deps.toBaseDeps(this, _sendCallback)}) {
        ABORT_IF_NOT(deps.valid(), "Invalid LocalTransport dependencies");
    }

    static constexpr const char *name = "LocalBufferedTransport";

    ReturnCode ackSend(const Envelope &req) { return ackSend(req.header); }

    ReturnCode ackSend(const Header &header) {
        FAIL_IF_ERR(_egressBuffer.release(header), ERR(OperationFailed),
                    "Failed to release frame from egress buffer for "
                    "LocalBufferedTransport for " MAGIC_PUBSUB_SV_FMT,
                    MAGIC_PUBSUB_SV_ARG(header));
        return OK();
    }

  private:
    ReturnCode _send(const Header &header, std::span<const std::byte> frame) {
        FAIL_IF_ERR_FWD(LocalTransport::_send(header, frame),
                        "Failed to send frame over LocalTransport");
        FAIL_IF_ERR_FWD(_egressBuffer.store(header, frame),
                        "Failed to store frame in egress buffer for "
                        "LocalBufferedTransport");
        return OK();
    }

    detail::EgressBuffer<LocalTransportEgressByteArenaConfig> _egressBuffer;

    using DefaultError = CoreError;
};

} // namespace Totem::PubSubBackend::Transports
