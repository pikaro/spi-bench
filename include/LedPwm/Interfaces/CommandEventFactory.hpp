#pragma once

#include "LedPwm/Interfaces/CommandEvent.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/detail/Pool.hpp"
#include "Services/PubSub.hpp"
#include "StaticConfig/LedPwm.hpp"
#include "Types/Error.hpp"

namespace Totem::LedPwm {

inline ReturnCode publishCommandEvent(CommandEvent event) {
    using Pool = Totem::PubSubBackend::Pool<
        CommandEvent, LedPwmConfig::commandPublishPoolSize>;
    static Pool pool{};

    FAIL_IF_NOT(PubSubService::configured(), ERR(CoreError, InvalidState),
                "PubSub backend is not configured");
    FAIL_IF_NOT(event.validate(), ERR(InvalidArgument),
                "Invalid LED PWM command event");

    auto &pubSub = PubSubService::get();
    const auto messageId = pubSub.nextMessageId();
    FAIL_IF(messageId == 0, ERR(CoreError, InvalidState),
            "PubSub returned message ID 0");

    auto stored = pool.store(event, messageId);
    if (!stored) {
        return stored.error();
    }

    auto envelopeResult =
        Totem::PubSubBackend::Envelope::make<CommandEvent>({
            .owner = static_cast<void *>(&pool),
            .topic = NodeData::PubSub::Topic::LedPwm,
            .messageId = messageId,
            .getPayloadPtr = Pool::getPtr,
            .encodePayload = Pool::encodePayload,
            .release = Pool::release,
            .requireSyncedClock = false,
        });
    if (!envelopeResult) {
        (void)pool.release({.header = {.messageId = messageId}});
        return envelopeResult.error();
    }

    auto publishResult = pubSub.publish(*envelopeResult);
    if (!publishResult.ok()) {
        (void)pool.release(*envelopeResult);
        return publishResult;
    }

    return OK();
}

} // namespace Totem::LedPwm
