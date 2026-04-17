#pragma once

#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/detail/SubscriberDirectory.hpp"
#include "PubSubBackend/detail/TransporterDirectory.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Support/Basic.hpp"
#include "Types/Error.hpp"
#include <optional>

namespace Totem::PubSubBackend::detail {

class Publisher {
    using TransporterKey = TransporterDirectory::EntryKey;
    using SubscriberKey = SubscriberDirectory::EntryKey;
    using Topic = typename Spec::Topic;
    using NodeId = typename Spec::NodeId;

  public:
    explicit Publisher(TransporterDirectory &transporters,
                       SubscriberDirectory &subscribers)
        : _transporters(transporters), _subscribers(subscribers) {}

    ReturnCode
    publish(FrameHandle frameHandle,
            std::optional<TransportId> ingressTransport = std::nullopt) {
        auto ret = OK();
        _log_d("Publisher: publish " MAGIC_PUBSUB_SV_FMT,
               MAGIC_PUBSUB_SV_ARG(frameHandle->envelope.header));
        ret.combine(publishToSubscribers(frameHandle->envelope));
        ret.combine(publishToTransports(frameHandle, ingressTransport));
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
        std::optional<TransportId> ingressTransport = std::nullopt) {
        _log_d("Publisher: fanout to transports for " MAGIC_PUBSUB_SV_FMT,
               MAGIC_PUBSUB_SV_ARG(frameHandle->envelope.header));
        return _transporters.withAll(
            [&](const TransporterKey &,
                const TransporterEntry &entry) -> ReturnCode {
                _log_d("Publisher: enqueue to transport " SV_FMT
                       " (%u) for " MAGIC_PUBSUB_SV_FMT,
                       SV_ARG(entry.name), entry.transportId,
                       MAGIC_PUBSUB_SV_ARG(frameHandle->envelope.header));
                FAIL_IF_ERR_FWD(
                    entry.transporter.enqueue(frameHandle),
                    "Failed to enqueue message to transport " SV_FMT
                    " for topic " SV_FMT,
                    SV_ARG(entry.name),
                    MAGIC_SV_ARG(Spec::Topic,
                                 frameHandle->envelope.header.topic));
                return OK();
            },
            [topicId = frameHandle->envelope.header.topic, ingressTransport](
                const TransporterKey &, const TransporterEntry &entry) -> bool {
                if (ingressTransport.has_value() &&
                    entry.transportId == *ingressTransport) {
                    return false;
                }
                return has_flag(entry.topicMask, topicId);
            });
    }

  private:
    TransporterDirectory &_transporters;
    SubscriberDirectory &_subscribers;
};

} // namespace Totem::PubSubBackend::detail
