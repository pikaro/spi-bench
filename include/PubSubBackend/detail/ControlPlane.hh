#pragma once

#include "Macros/Facade.hh"
#include "PubSubBackend/Interfaces/Frame.hh"
#include "PubSubBackend/detail/SubscriptionManager.hh"
#include "PubSubBackend/detail/Types.hh"
#include "Types/Error.hh"
#include "magic_enum/magic_enum.hpp"
#include <optional>

namespace Totem::PubSubBackend::detail {

class ControlPlane {
    using Topic = typename Spec::Topic;

  public:
    explicit ControlPlane(SubscriptionManager &subscriptionManager)
        : _subscriptionManager(subscriptionManager) {}

    ReturnCode
    handle(const PublishRequest &request,
           std::optional<TransportId> ingressTransport = std::nullopt) {
        if (!isControlPlaneTopic(request.topic)) {
            return OK();
        }

        auto topic = static_cast<Topic>(request.topic);
        if (topic == Topic::PubSub) {
            return _subscriptionManager.handlePubSubEvent(request,
                                                          ingressTransport);
        }

        FAIL(ERR(InvalidArgument),
             "Received message with invalid control plane topic " SV_FMT,
             SV_ARG(magic_enum::enum_name(topic)));
    }

    [[nodiscard]] static constexpr bool isControlPlaneTopic(TopicId topicId) {
        auto topic = static_cast<Topic>(topicId);
        return (topic == Topic::PubSub);
    }

  private:
    SubscriptionManager &_subscriptionManager;

    using DefaultError = CoreError;
};

} // namespace Totem::PubSubBackend::detail
