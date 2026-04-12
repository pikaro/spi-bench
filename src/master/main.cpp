#include "CommandBackend/Facade.hh"
#include "Config.hh"
#include "Data/Facade.hh"
#include "Macros/Facade.hh"
#include "Monitoring/Facade.hh"
#include "Output/Facade.hh"
#include "Platform/Uart.hh"
#include "Platform/platform/PlatformESP32/Base.hh"
#include "PubSubBackend/Facade.hh"
#include "PubSubBackend/Transports/LocalBufferedTransport.hh"
#include "PubSubBackend/Transports/LocalTransport.hh"
#include "Services/Commands.hh"
#include "Services/PubSub.hh"
#include "Support/CoreCommands.hh"
#include "TaskControllerRegistry/Facade.hh"
#include <cstdint>

Totem::TaskControllerRegistry::Registry taskRegistry;

Totem::Output::Aggregator aggregator(taskRegistry.hooks());
Totem::Output::UartOutput uart;

Totem::CommandBackend::Controller commandController(taskRegistry.hooks());
Totem::CommandBackend::UartTransport uartSource;
Totem::TaskControllerRegistry::SystemTaskSource systemTaskSource(taskRegistry);

Totem::Monitoring::Monitoring monitoring(taskRegistry);

using PubSubNode = Totem::PubSubBackend::Node;
PubSubNode pubSubNode(taskRegistry.hooks());
Totem::PubSubBackend::Transports::LocalTransport testPubSub1({
    .base =
        {
            .owner = static_cast<void *>(&pubSubNode),
            .transportId =
                static_cast<uint8_t>(NodeData::PubSub::Transport::SPI),
            .name = "SPI",
            .sendAckCallback = PubSubNode::ack,
            .ingress = pubSubNode.ingress(),
        },
});
Totem::PubSubBackend::Transports::LocalBufferedTransport testPubSub2({
    .base =
        {
            .owner = static_cast<void *>(&pubSubNode),
            .transportId =
                static_cast<uint8_t>(NodeData::PubSub::Transport::WebSocket),
            .name = "WebSocket",
            .sendAckCallback = PubSubNode::ack,
            .ingress = pubSubNode.ingress(),
        },
});

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

    ABORT_IF_ERR_BEGIN(pubSubNode.begin());
    ABORT_IF_ERR_BEGIN(testPubSub1.begin());
    ABORT_IF_ERR_BEGIN(testPubSub2.begin());
    ABORT_IF_ERR(testPubSub1.addLink(testPubSub2),
                 "Failed to link test PubSub transports together");
    ABORT_IF_UNEXPECTED(testHandle1, pubSubNode.registerTransport(testPubSub1),
                        "Failed to register local transport to PubSub node");
    ABORT_IF_UNEXPECTED(testHandle2, pubSubNode.registerTransport(testPubSub2),
                        "Failed to register local2 transport to PubSub node");
    PubSubService::setBackend(pubSubNode);

    (void)testHandle1;
    (void)testHandle2;

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

void app_main() {
    setup();
    for (;;) {
        if (auto reapResult = taskRegistry.reap(); !reapResult.ok()) {
            _log_e("Error during task registry reap: %s", reapResult.format());
        }
        ::platform::delay_until(&lastWakeTime, 1000);
    }
}
