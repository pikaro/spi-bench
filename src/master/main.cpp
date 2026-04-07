#include "Common.hh"

#include "CommandBackend/Facade.hh"
#include "MetricsBackend/Facade.hh"
#include "Monitoring/Facade.hh"
#include "Output/Facade.hh"
#include "Platform/Uart.hh"
#include "Platform/platform/PlatformESP32/Base.hh"
#include "Support/Commands.hh"
#include "Support/CoreCommands.hh"
#include "Support/Metrics.hh"
#include "TaskControllerRegistry/Facade.hh"

Totem::MetricsBackend::Backend metricsBackend;
Totem::TaskControllerRegistry::Registry taskRegistry;

Totem::Output::Aggregator aggregator(taskRegistry.hooks());
Totem::Output::UartOutput uart;

Totem::CommandBackend::Controller commandController(taskRegistry.hooks());
Totem::CommandBackend::UartTransport uartSource;

Totem::Monitoring::Monitoring monitoring(taskRegistry);

void setup() {
    ABORT_IF_ERR_BEGIN(::platform::Uart::init());

    ABORT_IF_ERR_BEGIN(taskRegistry.begin());

    ABORT_IF_ERR_BEGIN(uart.begin());

    ABORT_IF_ERR_BEGIN(commandController.begin());
    ABORT_IF_UNEXPECTED(uartTransport, uartSource.transport(),
                        "Failed to get transport from UART transport");
    ABORT_IF_ERR(commandController.addTransport(uartTransport),
                 "Failed to add UART transport to command controller");
    Commands::setBackend(commandController);

    ABORT_IF_ERR(register_core_commands(),
                 "Failed to register core commands to command controller");

    ABORT_IF_UNEXPECTED(uartSink, uart.sink(),
                        "Failed to get sink from UART output");

    ABORT_IF_ERR_BEGIN(aggregator.begin());
    ABORT_IF_ERR(aggregator.addSink(uartSink),
                 "Failed to add UART sink to aggregator");

    ABORT_IF_ERR(Logger::setBackend(aggregator),
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
        ::platform::delay_until(&lastWakeTime, 1000);
    }
}
