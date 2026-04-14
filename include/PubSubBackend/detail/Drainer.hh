#pragma once

#include "Macros/Facade.hh"
#include "PubSubBackend/Interfaces/Envelope.hh"
#include "PubSubBackend/detail/ControlPlane.hh"
#include "PubSubBackend/detail/Publisher.hh"
#include "PubSubBackend/detail/TransporterDirectory.hh"
#include "PubSubBackend/detail/Types.hh"
#include "Queue/Facade.hh"
#include "Types/Error.hh"
#include "magic_enum/magic_enum.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>

namespace Totem::PubSubBackend::detail {

struct DrainerDependencies {
    TransporterDirectory &transporters;
    Publisher &publisher;
    ControlPlane &controlPlane;
    Totem::Queue::Handle *publishQueue;

    [[nodiscard]] bool validate() const { return publishQueue != nullptr; }
};

class Drainer {
    using TransporterKey = TransporterDirectory::EntryKey;
    using Topic = typename Spec::Topic;
    using NodeId = typename Spec::NodeId;

    static constexpr auto kMaxInFlightMessages =
        Spec::Limits::maxInFlightMessages;

  public:
    explicit Drainer(const DrainerDependencies &deps)
        : _transporters(deps.transporters), _publisher(deps.publisher),
          _controlPlane(deps.controlPlane), _publishQueue(deps.publishQueue) {
        ABORT_IF_NOT(deps.validate(), "Invalid Drainer dependencies");
    }

    ReturnCode drain() {
        auto ret = OK();
        ret.combine(_publishFromQueue());
        ret.combine(_publishFromTransports());
        return ret;
    }

    ReturnCode ack(Spec::Transport transportId, const Envelope &envelope) {
        auto ret = OK();
        auto mask = static_cast<TransportMask>(transportId);
        for (size_t i = 0; i < kMaxInFlightMessages; ++i) {
            auto &frame = _inFlightFrames[i];
            if (frame.valid() && (frame.pendingMask & mask) != 0) {
                if (frame.envelope.header.messageId ==
                    envelope.header.messageId) {
                    _log_d("Drainer: ack from transport " SV_FMT
                           " for " MAGIC_PUBSUB_SV_FMT,
                           SV_ARG(magic_enum::enum_name(transportId)),
                           MAGIC_PUBSUB_SV_ARG(envelope.header));
                    frame.pendingMask &= ~mask;
                    if (--frame.pendingCount == 0) {
                        _log_d("Drainer: final ack for " MAGIC_PUBSUB_SV_FMT,
                               MAGIC_PUBSUB_SV_ARG(envelope.header));
                        if (frame.envelope.release != nullptr) {
                            ret.combine(frame.envelope.release(
                                frame.envelope.owner, envelope));
                        }
                        frame = StoredFrame{};
                    }
                    return ret;
                }
            }
        }
        FAIL(ERR(NotFound),
             "No in-flight message found for ack with messageId "
             "%u from transport " SV_FMT,
             envelope.header.messageId,
             SV_ARG(magic_enum::enum_name(transportId)));
    }

  private:
    ReturnCode _publishFromQueue() {
        Envelope item;
        auto ret = OK();
        while (ret.ok()) {
            ret.combine(
                Totem::Queue::Platform::receive(*_publishQueue, &item, 0));
            if (ret.ok()) {
                _log_d("Drainer: dequeued local publish " MAGIC_PUBSUB_SV_FMT,
                       MAGIC_PUBSUB_SV_ARG(item.header));
                ret.combine(_publishFrame(item));
            }
        }
        if (ret == ERR(Timeout)) {
            return OK();
        }
        FAIL(ret, "Failed to receive publish envelope from queue: " ERR_FMT,
             ERR_ARG(ret));
    }

    struct PublishContext {
        Drainer *self;
        TransportId ingressTransport;
    };

    static ReturnCode _publishCallback(void *ctx, const Envelope &envelope) {
        auto *publishContext = static_cast<PublishContext *>(ctx);
        return publishContext->self->_publishFrame(
            envelope, publishContext->ingressTransport);
    }

    ReturnCode _publishFromTransports() {
        return _transporters.withAllConst(
            [&](const TransporterKey &,
                const TransporterEntry &entry) -> ReturnCode {
                auto ctx = PublishContext{
                    .self = this, .ingressTransport = entry.transportId};
                return entry.transporter.pollInto(&ctx, &_publishCallback);
            });
    }

    std::expected<FrameHandle, ReturnCode>
    _storeFrame(const Envelope &envelope) {
        StoredFrame frame;
        frame.envelope = envelope;
        TransportMask mask = 0;
        uint8_t pendingCount = 0;
        (void)_transporters.withAll(
            [&](const TransporterKey &,
                const TransporterEntry &entry) -> ReturnCode {
                if ((entry.topicMask & envelope.header.topic) != 0) {
                    mask |= entry.transportId;
                    ++pendingCount;
                }
                return OK();
            });
        if (pendingCount == 0) {
            _log_d("Drainer: no interested transports for " MAGIC_PUBSUB_SV_FMT,
                   MAGIC_PUBSUB_SV_ARG(envelope.header));
            return std::unexpected(ERR(NotFound));
        }
        frame.pendingMask = mask;
        frame.pendingCount = pendingCount;
        int16_t stored = -1;
        for (size_t i = 0; i < kMaxInFlightMessages; ++i) {
            if (_inFlightFrames[i].pendingCount == 0) {
                _inFlightFrames[i] = frame;
                stored = static_cast<int16_t>(i);
                break;
            }
        }
        FAIL_IF(stored < 0, std::unexpected(ERR(Overflow)),
                "No space to store in-flight message");
        _log_d("Drainer: stored in-flight message in slot %d with pendingMask "
               "0x%02x and pendingCount %u for " MAGIC_PUBSUB_SV_FMT,
               stored, static_cast<unsigned>(mask), pendingCount,
               MAGIC_PUBSUB_SV_ARG(envelope.header));
        return &_inFlightFrames[static_cast<size_t>(stored)];
    }

    ReturnCode
    _publishFrame(const Envelope &item,
                  std::optional<TransportId> ingressTransport = std::nullopt) {
        auto ret = OK();
        _log_d("Drainer: publish frame " MAGIC_PUBSUB_SV_FMT "%s",
               MAGIC_PUBSUB_SV_ARG(item.header),
               ingressTransport.has_value() ? " from transport ingress" : "");
        ret.combine(_controlPlane.handle(item, ingressTransport));
        ret.combine(_publisher.publishToSubscribers(item));
        auto storeResult = _storeFrame(item);
        if (!storeResult) {
            if (storeResult.error() == ERR(NotFound)) {
                _log_d("Drainer: releasing " MAGIC_PUBSUB_SV_FMT
                       " without transport fanout",
                       MAGIC_PUBSUB_SV_ARG(item.header));
                FAIL_IF_ERR_FWD(item.release(item.owner, item),
                                "Failed to release message for topic " SV_FMT
                                " with no subscribers or transports",
                                MAGIC_SV_ARG(Spec::Topic, item.header.topic));
                return ret;
            }
            FAIL(storeResult.error(),
                 "Failed to store in-flight message: " ERR_FMT,
                 ERR_ARG(storeResult.error()));
        }
        ret.combine(
            _publisher.publishToTransports(*storeResult, ingressTransport));
        return ret;
    }

    TransporterDirectory &_transporters;
    Publisher &_publisher;
    ControlPlane &_controlPlane;
    Totem::Queue::Handle *_publishQueue;
    std::array<StoredFrame, kMaxInFlightMessages> _inFlightFrames{};
};

} // namespace Totem::PubSubBackend::detail
