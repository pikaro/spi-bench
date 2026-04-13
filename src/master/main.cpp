#include "CommandBackend/Facade.hh"
#include "Data.hh"
#include "Data/Facade.hh"
#include "Macros/Facade.hh"
#include "Monitoring/Facade.hh"
#include "Output/Facade.hh"
#include "Platform/Uart.hh"
#include "Platform/platform/PlatformESP32/Base.hh"
#include "PubSubBackend/Facade.hh"
#include "PubSubBackend/Interfaces/Envelope.hh"
#include "PubSubBackend/Transports/LocalBufferedTransport.hh"
#include "PubSubBackend/Transports/LocalTransport.hh"
#include "Services/Commands.hh"
#include "Services/PubSub.hh"
#include "Support/CoreCommands.hh"
#include "TaskControllerRegistry/Facade.hh"
#include "TestMessage.hh"
#include "master/PubSubTest.hh"
#include <cstdint>
#include <utility>

Totem::TaskControllerRegistry::Registry taskRegistry;

Totem::Output::Aggregator aggregator(taskRegistry.hooks());
Totem::Output::UartOutput uart;

Totem::CommandBackend::Controller commandController(taskRegistry.hooks());
Totem::CommandBackend::UartTransport uartSource;
Totem::TaskControllerRegistry::SystemTaskSource systemTaskSource(taskRegistry);

Totem::Monitoring::Monitoring monitoring(taskRegistry);

using PubSubNode = Totem::PubSubBackend::Node;
PubSubNode pubSubNode1(taskRegistry.hooks());
PubSubNode pubSubNode2(taskRegistry.hooks());

Totem::PubSubBackend::Transports::LocalTransport testPubSub1({
    .base =
        {
            .pubSubNode = static_cast<void *>(&pubSubNode1),
            .transportId =
                static_cast<uint8_t>(NodeData::PubSub::Transport::SPI),
            .name = "SPI",
            .sendAckCallback = PubSubNode::ack,
            .ingress = pubSubNode1.ingress(),
        },
});

Totem::PubSubBackend::Transports::LocalBufferedTransport testPubSub2({
    .base =
        {
            .pubSubNode = static_cast<void *>(&pubSubNode2),
            .transportId =
                static_cast<uint8_t>(NodeData::PubSub::Transport::WebSocket),
            .name = "WebSocket",
            .sendAckCallback = PubSubNode::ack,
            .ingress = pubSubNode2.ingress(),
        },
});

Foo foo1;
Foo foo2;
Foo foo3;

void setup() {
    ABORT_IF_ERR_BEGIN(::platform::Uart::init());

    ABORT_IF_ERR_BEGIN(taskRegistry.begin());

    ABORT_IF_ERR_BEGIN(uart.begin());

    ABORT_IF_ERR_BEGIN(commandController.begin());
    ABORT_IF_UNEXPECTED(uartTransport, uartSource.transport(),
                        "Failed to get transport from UART transport");
    ABORT_IF_ERR(commandController.addTransport(uartTransport),
                 "Failed to add UART transport to command controller");
    CommandService::setBackend(commandController);

    ABORT_IF_ERR_BEGIN(pubSubNode1.begin());
    ABORT_IF_ERR_BEGIN(pubSubNode2.begin());

    ABORT_IF_ERR_BEGIN(testPubSub1.begin());
    ABORT_IF_ERR_BEGIN(testPubSub2.begin());

    ABORT_IF_ERR(testPubSub1.addLink(testPubSub2),
                 "Failed to link test PubSub transports together");

    ABORT_IF_UNEXPECTED(testHandle1, pubSubNode1.registerTransport(testPubSub1),
                        "Failed to register local transport to PubSub node");
    ABORT_IF_UNEXPECTED(testHandle2, pubSubNode2.registerTransport(testPubSub2),
                        "Failed to register local2 transport to PubSub node");

    PubSubService::setBackend(pubSubNode1);

    (void)testHandle1;
    (void)testHandle2;

    pubSubNode1.subscribe("foo1node1fft",
                          {.subscriber = &foo1, .callback = Foo::callback},
                          NodeData::PubSub::Topic::FftFrame);
    pubSubNode2.subscribe("foo1node2fft",
                          {.subscriber = &foo1, .callback = Foo::callback},
                          NodeData::PubSub::Topic::FftFrame);
    pubSubNode2.subscribe("foo2node2metrics",
                          {.subscriber = &foo2, .callback = Foo::callback},
                          NodeData::PubSub::Topic::Metrics);
    pubSubNode2.subscribe("foo2node2sensor",
                          {.subscriber = &foo2, .callback = Foo::callback},
                          NodeData::PubSub::Topic::Sensor);
    pubSubNode2.subscribe("foo1node2heartbeat",
                          {.subscriber = &foo1, .callback = Foo::callback},
                          NodeData::PubSub::Topic::Heartbeat);
    pubSubNode2.subscribe("foo2node2heartbeat",
                          {.subscriber = &foo2, .callback = Foo::callback},
                          NodeData::PubSub::Topic::Heartbeat);
    pubSubNode2.subscribe("foo3node2heartbeat",
                          {.subscriber = &foo3, .callback = Foo::callback},
                          NodeData::PubSub::Topic::Heartbeat);

    ABORT_IF_ERR(register_core_commands(),
                 "Failed to register core commands to command controller");

    ABORT_IF_UNEXPECTED(uartSink, uart.sink(),
                        "Failed to get sink from UART output");

    ABORT_IF_ERR_BEGIN(aggregator.begin());
    ABORT_IF_ERR(aggregator.addSink(uartSink),
                 "Failed to add UART sink to aggregator");

    ABORT_IF_ERR(LoggingService::setBackend(aggregator),
                 "Failed to set logger backend to aggregator");

    ABORT_IF_ERR_BEGIN(monitoring.begin());

    _log_i("Setup complete");

    _log_d("This is a debug message");
    _log_i("This is an info message");
    _log_w("This is a warning message");
    _log_e("This is an error message");
}

extern "C" {
void app_main(void);
}

::platform::Tick lastWakeTime;

using testPool = Totem::PubSubBackend::Pool<Message, 64>;
testPool messagePool{static_cast<void *>(&pubSubNode1),
                     PubSubNode::nextMessageId};

void app_main() {
    setup();
    for (;;) {
        if (auto reapResult = taskRegistry.reap(); !reapResult.ok()) {
            _log_e("Error during task registry reap: " ERR_FMT,
                   ERR_ARG(reapResult));
        }

        auto event = make_test_event();
        ABORT_IF_UNEXPECTED(
            messageId, messagePool.store(event.message),
            "Failed to allocate message from pool for topic " SV_FMT,
            SV_ARG(magic_enum::enum_name(event.topic)));
        auto envelopeDef = Totem::PubSubBackend::EnvelopeDef{
            .owner = static_cast<void *>(&messagePool),
            .topic = event.topic,
            .messageId = messageId,
            .getPayloadPtr = testPool::getPtr,
            .encodePayload = testPool::encodePayload,
            .release = testPool::release,
        };
        auto envelopeResult =
            Totem::PubSubBackend::Envelope::make<Message>(envelopeDef);
        auto result = pubSubNode1.publish(std::move(envelopeResult).value());

        if (!result.ok()) {
            _log_e("Failed to publish message: " ERR_FMT, ERR_ARG(result));
        }

        ::platform::delay_until(&lastWakeTime, 1000);
    }
}
