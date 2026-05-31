#pragma once

#include "Buttons/Interfaces/Wire.hpp"
#include "Data.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "Services/PubSub.hpp"
#include "Types/Error.hpp"

namespace Totem::Buttons {

inline ReturnCode publishButtonEvent(ButtonEvent event) {
    static PubSubBackend::Pool<ButtonEvent, 8> pool{
        PubSubService::nextMessageId};

    FAIL_IF_NOT(PubSubService::configured(), ERR(CoreError, InvalidState),
                "PubSub backend is not configured for button event publish");

    auto stored = pool.store(event);
    if (!stored) {
        FAIL_ERR_FWD(stored.error(),
                     "Failed to store button event in PubSub pool");
    }

    auto envelopeResult =
        PubSubBackend::Envelope::make<ButtonEvent>({
            .owner = static_cast<void *>(&pool),
            .topic = NodeData::PubSub::Topic::Button,
            .messageId = *stored,
            .getPayloadPtr = PubSubBackend::Pool<ButtonEvent, 8>::getPtr,
            .encodePayload =
                PubSubBackend::Pool<ButtonEvent, 8>::encodePayload,
            .release = PubSubBackend::Pool<ButtonEvent, 8>::release,
            .requireSyncedClock = false,
        });
    if (!envelopeResult) {
        (void)pool.release({.header = {.messageId = *stored}});
        FAIL_ERR_FWD(envelopeResult.error(),
                     "Failed to create PubSub envelope for button event");
    }

    auto publishResult = PubSubService::get().publish(*envelopeResult);
    if (!publishResult.ok()) {
        (void)pool.release(*envelopeResult);
        FAIL_ERR_FWD(publishResult,
                     "Failed to publish button event envelope to PubSub");
    }
    return OK();
}

inline ReturnCode publishPressed(PeripheralButton button) {
    return publishButtonEvent({
        .type = ButtonEventType::Pressed,
        .button = button,
    });
}

} // namespace Totem::Buttons
