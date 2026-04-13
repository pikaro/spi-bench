#pragma once

#include "Data/PubSub.hh"
#include "Macros/Facade.hh"
#include "Macros/internal/Markers.hh"
#include "PubSubBackend/Interfaces/Envelope.hh"
#include "TestMessage.hh"
#include "Types/Error.hh"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

class Foo {
  public:
    static ReturnCode callback(void *ctx,
                               const Totem::PubSubBackend::Envelope &envelope) {
        auto *self = static_cast<Foo *>(ctx);
        return self->handleMessage(envelope);
    }

    ReturnCode handleMessage(const Totem::PubSubBackend::Envelope &envelope) {
        _shutUpClang = true;
        FAIL_IF_UNEXPECTED_FWD(message, envelope.getPayloadAs<Message>(),
                               "Failed to decode message payload");
        _log_i("Flag: %s", message.flag ? "true" : "false");
        _log_i("Int value: %d", message.intVal);
        _log_i("Uint32 value: %u", message.uint32Val);
        _log_i("Uint16 value: %u", message.uint16Val);
        _log_i("Uint8 value: %u", message.uint8Val);
        _log_i("String value: %s", message.strVal);
        _log_i("Byte array value:");
        for (size_t i = 0; i < message.byteArrayVal.size(); ++i) {
            _log_i("  [%zu]: %02x", i,
                   std::to_integer<uint8_t>(message.byteArrayVal[i]));
        }
        return OK();
    }

  private:
    bool _shutUpClang = false;

    using DefaultError = CoreError;
};

static Message make_test_message() {
    Message msg{};
    msg.flag = std::rand() % 2 == 0;
    msg.intVal = std::rand() % 1000;
    msg.uint32Val = std::rand() % 100000;
    msg.uint16Val = std::rand() % 65536;
    msg.uint8Val = std::rand() % 256;
    msg.strVal = "Hello, PubSub!";
    for (size_t i = 0; i < msg.byteArrayVal.size(); ++i) {
        msg.byteArrayVal[i] = std::byte(std::rand() % 256);
    }
    return msg;
}

struct Event {
    Message message;
    Totem::Data::PubSub::Topic topic;
};

inline Event make_test_event() {
    Event event{};
    event.message = make_test_message();
    event.topic = static_cast<Totem::Data::PubSub::Topic>(std::rand() % 7);
    return event;
}
