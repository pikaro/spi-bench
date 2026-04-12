#pragma once

#include "Macros/Facade.hh"
#include "PubSubBackend/Interfaces/Frame.hh"
#include "PubSubBackend/detail/SubscriberDirectory.hh"
#include "PubSubBackend/detail/TransporterDirectory.hh"
#include "PubSubBackend/detail/Types.hh"
#include "Support/Basic.hh"
#include "Types/Error.hh"
#include "magic_enum/magic_enum.hpp"
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
        ret.combine(publishToSubscribers(frameHandle->request));
        ret.combine(publishToTransports(frameHandle, ingressTransport));
        return ret;
    }

    ReturnCode publishToSubscribers(const PublishRequest &req) {
        return _subscribers.withAll(
            [&](const SubscriberNameKey &nameKey,
                const SubscriberEntry &entry) -> ReturnCode {
                FAIL_IF_ERR_FWD(entry.callback(req),
                                "Failed to publish to subscriber " SV_FMT
                                " for topic " SV_FMT,
                                SV_ARG(nameKey.name),
                                SV_ARG(magic_enum::enum_name(
                                    static_cast<Spec::Topic>(req.topic))));
                return OK();
            },
            [topicId = req.topic](const SubscriberNameKey & /* unused*/,
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
                    SV_ARG(magic_enum::enum_name(
                        static_cast<Spec::Topic>(frameHandle->request.topic))));
                return OK();
            },
            [topicId = frameHandle->request.topic,
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
