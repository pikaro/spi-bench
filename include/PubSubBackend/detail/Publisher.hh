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
    using TransporterNameKey = TransporterDirectory::EntryNameKey;
    using SubscriberNameKey = SubscriberDirectory::EntryNameKey;

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

    ReturnCode publishToSubscribers(const Envelope &req) {
        return _subscribers.withAll(
            [&](const SubscriberNameKey &nameKey,
                const SubscriberEntry &entry) -> ReturnCode {
                FAIL_IF_ERR_FWD(entry.callback(req),
                                "Failed to publish to subscriber " SV_FMT
                                " for topic " SV_FMT,
                                SV_ARG(nameKey.name),
                                MAGIC_SV_ARG(Spec::Topic, req.header.topic));
                return OK();
            },
            [topicId = req.header.topic](const SubscriberNameKey & /* unused*/,
                                         const SubscriberEntry &entry) -> bool {
                return entry.topic == topicId;
            });
    }

    ReturnCode publishToTransports(
        FrameHandle frameHandle,
        std::optional<TransportId> ingressTransport = std::nullopt) {
        return _transporters.withAll(
            [&](const TransporterNameKey &nameKey,
                const TransporterEntry &entry) -> ReturnCode {
                FAIL_IF_ERR_FWD(
                    entry.transporter.enqueue(frameHandle),
                    "Failed to enqueue message to transport " SV_FMT
                    " for topic " SV_FMT,
                    SV_ARG(nameKey.name),
                    MAGIC_SV_ARG(Spec::Topic,
                                 frameHandle->envelope.header.topic));
                return OK();
            },
            [topicId = frameHandle->envelope.header.topic,
             ingressTransport](const TransporterNameKey & /* unused*/,
                               const TransporterEntry &entry) -> bool {
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
