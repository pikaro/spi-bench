#pragma once

#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "PubSubBackend/detail/SubscriberDirectory.hpp"
#include "PubSubBackend/detail/TransportDirectory.hpp"
#include "PubSubBackend/detail/Trace.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Support/Basic.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <optional>

namespace Totem::PubSubBackend::detail {

class Publisher {
    using Topic = typename Spec::Topic;
    using NodeId = typename Spec::NodeId;

  public:
    explicit Publisher(TransportDirectory &transporters,
                       SubscriberDirectory &subscribers)
        : _transporters(transporters), _subscribers(subscribers) {}

    [[nodiscard]] static std::optional<TransportDispatch>
    dispatchFor(const TransporterEntry &entry, const Header &header,
                std::optional<IngressContext> ingressContext = std::nullopt) {
        auto dispatch = TransportDispatch{.ingress = ingressContext};
        const auto sameTransportIngress =
            ingressContext.has_value() &&
            entry.transportId == ingressContext->transportId;

        if (!entry.transporter->available()) {
            return std::nullopt;
        }

        if (entry.forwardingPolicy ==
            TransportForwardingPolicy::SharedBusRouter) {
            const auto availablePeerMask = entry.transporter->knownPeers();
            dispatch.targetPeers =
                header.topic == static_cast<TopicId>(Spec::Topic::PubSub)
                    ? availablePeerMask
                    : static_cast<PeerMask>(
                          _peerMaskForTopic(entry, header.topic) &
                          availablePeerMask);
            if (sameTransportIngress && ingressContext->hasPeer()) {
                dispatch.targetPeers &= ~ingressContext->peerId;
            }
            if (!dispatch.hasTargetPeers()) {
                return std::nullopt;
            }
            return dispatch;
        }

        if (sameTransportIngress) {
            return std::nullopt;
        }
        if ((entry.topicMask & header.topic) == 0) {
            return std::nullopt;
        }
        return dispatch;
    }

    [[nodiscard]] static std::optional<TransportDispatch>
    dispatchFor(const TransporterEntry &entry, const Envelope &envelope,
                std::optional<IngressContext> ingressContext = std::nullopt) {
        return dispatchFor(entry, envelope.header, ingressContext);
    }

    ReturnCode
    publish(FrameHandle frameHandle,
            std::optional<IngressContext> ingressContext = std::nullopt) {
        auto ret = OK();
        _log_d("Publisher: publish " MAGIC_PUBSUB_SV_FMT,
               MAGIC_PUBSUB_SV_ARG(frameHandle->envelope.header));
        ret.combine(publishToSubscribers(frameHandle->envelope));
        ret.combine(publishToTransports(frameHandle, ingressContext));
        return ret;
    }

    ReturnCode publishToSubscribers(const Envelope &envelope) {
        _log_d("Publisher: fanout to subscribers for " MAGIC_PUBSUB_SV_FMT,
               MAGIC_PUBSUB_SV_ARG(envelope.header));
        return _subscribers.withAll(
            [&](const SubscriberKey &,
                const SubscriberEntry &entry) -> ReturnCode {
                _log_d("Publisher: delivering to subscriber " SV_FMT
                       " for " MAGIC_PUBSUB_SV_FMT,
                       SV_ARG(entry.name),
                       MAGIC_PUBSUB_SV_ARG(envelope.header));
                log_trace_packet("publisher.subscriber", envelope.header,
                                 entry.name.data());
                FAIL_IF_ERR_FWD(
                    entry.callback(entry.subscriber, envelope),
                    "Failed to publish to subscriber " SV_FMT
                    " for topic " SV_FMT,
                    SV_ARG(entry.name),
                    MAGIC_SV_ARG(Spec::Topic, envelope.header.topic));
                return OK();
            },
            [topicId = envelope.header.topic](
                const SubscriberKey &, const SubscriberEntry &entry) -> bool {
                return entry.topic == topicId;
            });
    }

    ReturnCode publishToTransports(
        FrameHandle frameHandle,
        std::optional<IngressContext> ingressContext = std::nullopt) {
        _log_d("Publisher: fanout to transports for " MAGIC_PUBSUB_SV_FMT,
               MAGIC_PUBSUB_SV_ARG(frameHandle->envelope.header));
        return _transporters.withAll(
            [&](const TransportId &,
                const TransporterEntry &entry) -> ReturnCode {
                auto dispatch =
                    dispatchFor(entry, frameHandle->envelope, ingressContext);
                if (!dispatch) {
                    return OK();
                }

                _log_d("Publisher: enqueue to transport " SV_FMT
                       " (%u) for " MAGIC_PUBSUB_SV_FMT,
                       SV_ARG(entry.name), entry.transportId,
                       MAGIC_PUBSUB_SV_ARG(frameHandle->envelope.header));
                FAIL_IF_ERR_FWD(
                    entry.transporter->enqueue(frameHandle, *dispatch),
                    "Failed to enqueue message to transport " SV_FMT
                    " for topic " SV_FMT,
                    SV_ARG(entry.name),
                    MAGIC_SV_ARG(Spec::Topic,
                                 frameHandle->envelope.header.topic));
                return OK();
            });
    }

  private:
    [[nodiscard]] static PeerMask
    _peerMaskForTopic(const TransporterEntry &entry, TopicId topicId) {
        PeerMask out = 0;
        for (size_t i = 0; i < entry.peerTopicMasks.size(); ++i) {
            if ((entry.peerTopicMasks[i] & topicId) != 0) {
                out |= static_cast<PeerMask>(1U << i);
            }
        }
        return out;
    }

    TransportDirectory &_transporters;
    SubscriberDirectory &_subscribers;
};

} // namespace Totem::PubSubBackend::detail
