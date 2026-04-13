#pragma once

#include "Macros/Facade.hh"
#include "PubSubBackend/Interfaces/Envelope.hh"
#include "PubSubBackend/detail/SubscriberDirectory.hh"
#include "PubSubBackend/detail/TransporterDirectory.hh"
#include "PubSubBackend/detail/Types.hh"
#include "Support/Basic.hh"
#include "Types/Error.hh"
#include <optional>

namespace Totem::PubSubBackend::detail {

class Publisher {
    using TransporterKey = TransporterDirectory::EntryKey;
    using SubscriberKey = SubscriberDirectory::EntryKey;

  public:
    explicit Publisher(TransporterDirectory &transporters,
                       SubscriberDirectory &subscribers)
        : _transporters(transporters), _subscribers(subscribers) {}

    ReturnCode
    publish(FrameHandle frameHandle,
            std::optional<TransportId> ingressTransport = std::nullopt) {
        auto ret = OK();
        ret.combine(publishToSubscribers(frameHandle->envelope));
        ret.combine(publishToTransports(frameHandle, ingressTransport));
        return ret;
    }

    ReturnCode publishToSubscribers(const Envelope &envelope) {
        return _subscribers.withAll(
            [&](const SubscriberKey &,
                const SubscriberEntry &entry) -> ReturnCode {
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
        return _transporters.withAll(
            [&](const TransporterKey &,
                const TransporterEntry &entry) -> ReturnCode {
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

    using DefaultError = CoreError;
};

} // namespace Totem::PubSubBackend::detail
