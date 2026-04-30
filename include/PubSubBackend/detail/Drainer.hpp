#pragma once

#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/detail/ControlPlane.hpp"
#include "PubSubBackend/detail/Publisher.hpp"
#include "PubSubBackend/detail/TransportDirectory.hpp"
#include "PubSubBackend/detail/Trace.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Queue/Facade.hpp"
#include "Types/Error.hpp"
#include "magic_enum/magic_enum.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>

namespace Totem::PubSubBackend::detail {

struct DrainerDependencies {
    TransportDirectory &transporters;
    Publisher &publisher;
    ControlPlane &controlPlane;
    Totem::Queue::Handle *publishQueue;

    [[nodiscard]] bool validate() const { return publishQueue != nullptr; }
};

class Drainer {
    using TransporterKey = TransportDirectory::EntryKey;
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
                if (frame.envelope.header == envelope.header) {
                    _log_d("Drainer: ack from transport " SV_FMT
                           " for " MAGIC_PUBSUB_SV_FMT,
                           SV_ARG(magic_enum::enum_name(transportId)),
                           MAGIC_PUBSUB_SV_ARG(envelope.header));
                    frame.pendingMask &= ~mask;
                    if (--frame.pendingCount == 0) {
                        _log_d("Drainer: final ack for " MAGIC_PUBSUB_SV_FMT,
                               MAGIC_PUBSUB_SV_ARG(envelope.header));
                        if (frame.envelope.release != nullptr) {
                            ret.combine(frame.envelope.ack());
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
    struct TransportTarget {
        ITransport *transporter = nullptr;
        TransportDispatch dispatch{};
        TransportId transportId = 0;
        std::string_view name{};
    };

    ReturnCode _publishFromQueue() {
        Envelope item;
        auto ret = OK();
        while (ret.ok()) {
            ret.combine(
                Totem::Queue::Platform::receive(*_publishQueue, &item, 0));
            if (ret.ok()) {
                log_trace_packet("drainer.local.dequeue", item.header,
                                 "Drainer");
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
        TransportId transportId = 0;
    };

    static ReturnCode _publishCallback(
        void *ctx, const Envelope &envelope,
        std::optional<IngressContext> ingressContext = std::nullopt) {
        auto *publishContext = static_cast<PublishContext *>(ctx);
        if (!ingressContext.has_value()) {
            ingressContext = IngressContext{
                .transportId = publishContext->transportId,
            };
        }
        return publishContext->self->_publishFrame(envelope, ingressContext);
    }

    ReturnCode _publishFromTransports() {
        FAIL_IF_UNEXPECTED_FWD(
            snapshot, _transporters.snapshot(),
            "Failed to snapshot PubSub transporters for polling");
        auto ret = OK();
        for (size_t i = 0; i < snapshot.count; ++i) {
            const auto &entry = snapshot.entries[i];
            auto ctx = PublishContext{
                .self = this,
                .transportId = entry.transportId,
            };
            ret.combine(entry.transporter->pollInto(&ctx, &_publishCallback));
        }
        return ret;
    }

    std::expected<FrameHandle, ReturnCode> _storeFrame(const Envelope &envelope,
                                                       TransportMask mask,
                                                       uint8_t pendingCount) {
        StoredFrame frame;
        frame.envelope = envelope;
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

    std::expected<size_t, ReturnCode>
    _collectTransportTargets(
        const Envelope &item, std::optional<IngressContext> ingressContext,
        std::span<TransportTarget> targets, TransportMask &mask,
        uint8_t &pendingCount) {
        size_t targetCount = 0;
        mask = 0;
        pendingCount = 0;
        FAIL_IF_ERR_FWD_UNEXPECTED(
            _transporters.withAll(
                [&](const TransporterKey &,
                    const TransporterEntry &entry) -> ReturnCode {
                    auto dispatch =
                        Publisher::dispatchFor(entry, item, ingressContext);
                    if (!dispatch) {
                        return OK();
                    }
                    FAIL_IF(targetCount >= targets.size(), ERR(Overflow),
                            "Too many PubSub transport fanout targets");
                    targets[targetCount++] = TransportTarget{
                        .transporter = entry.transporter,
                        .dispatch = *dispatch,
                        .transportId = entry.transportId,
                        .name = entry.name,
                    };
                    mask |= entry.transportId;
                    ++pendingCount;
                    return OK();
                }),
            "Failed to collect transport targets for PubSub frame");
        return targetCount;
    }

    ReturnCode
    _publishFrame(const Envelope &item,
                  std::optional<IngressContext> ingressContext = std::nullopt) {
        auto ret = OK();
        log_trace_packet(ingressContext.has_value()
                             ? "drainer.ingress.publish"
                             : "drainer.local.publish",
                         item.header, "Drainer");
        _log_d("Drainer: publish frame " MAGIC_PUBSUB_SV_FMT "%s",
               MAGIC_PUBSUB_SV_ARG(item.header),
               ingressContext.has_value() ? " from transport ingress" : "");
        ret.combine(_controlPlane.handle(item, ingressContext));
        ret.combine(_publisher.publishToSubscribers(item));
        std::array<TransportTarget, Spec::Limits::maxTransports> targets{};
        TransportMask pendingMask = 0;
        uint8_t pendingCount = 0;
        FAIL_IF_UNEXPECTED_FWD(
            targetCount,
            _collectTransportTargets(item, ingressContext, targets, pendingMask,
                                     pendingCount),
            "Failed to route PubSub frame to transports");
        if (targetCount == 0) {
            _log_d("Drainer: releasing " MAGIC_PUBSUB_SV_FMT
                   " without transport fanout",
                   MAGIC_PUBSUB_SV_ARG(item.header));
            FAIL_IF_ERR_FWD(item.ack(),
                            "Failed to release message for topic " SV_FMT
                            " with no subscribers or transports",
                            MAGIC_SV_ARG(Spec::Topic, item.header.topic));
            return ret;
        }

        auto storeResult = _storeFrame(item, pendingMask, pendingCount);
        if (!storeResult) {
            FAIL(storeResult.error(),
                 "Failed to store in-flight message: " ERR_FMT,
                 ERR_ARG(storeResult.error()));
        }
        for (size_t i = 0; i < targetCount; ++i) {
            const auto &target = targets[i];
            if ((*storeResult)->pendingCount == 0) {
                break;
            }
            log_trace_packet("drainer.transport.enqueue", item.header,
                             target.name.data());
            _log_d("Drainer: enqueue to transport " SV_FMT
                   " (%u) for " MAGIC_PUBSUB_SV_FMT,
                   SV_ARG(target.name), target.transportId,
                   MAGIC_PUBSUB_SV_ARG(item.header));
            auto enqueueRet =
                target.transporter->enqueue(*storeResult, target.dispatch);
            if (!enqueueRet.ok()) {
                _log_w("Drainer: dropping transport target " SV_FMT
                       " for " MAGIC_PUBSUB_SV_FMT " after enqueue failed: "
                       ERR_FMT,
                       SV_ARG(target.name),
                       MAGIC_PUBSUB_SV_ARG(item.header),
                       ERR_ARG(enqueueRet));
                FAIL_IF_ERR_FWD(_releaseTransportTarget(
                                    **storeResult, target.transportId),
                                "Failed to release failed transport target");
            }
        }
        return ret;
    }

    ReturnCode _releaseTransportTarget(StoredFrame &frame,
                                       TransportId transportId) {
        if (frame.pendingCount == 0) {
            return OK();
        }
        const auto mask = static_cast<TransportMask>(transportId);
        if ((frame.pendingMask & mask) == 0) {
            return OK();
        }
        frame.pendingMask &= ~mask;
        --frame.pendingCount;
        if (frame.pendingCount != 0) {
            return OK();
        }
        auto envelope = frame.envelope;
        frame = StoredFrame{};
        if (envelope.release == nullptr) {
            return OK();
        }
        return envelope.ack();
    }

    TransportDirectory &_transporters;
    Publisher &_publisher;
    ControlPlane &_controlPlane;
    Totem::Queue::Handle *_publishQueue;
    std::array<StoredFrame, kMaxInFlightMessages> _inFlightFrames{};
};

} // namespace Totem::PubSubBackend::detail
