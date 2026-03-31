#include "Common.hh"

#include "Output/Facade.hh"
#include "TaskControllerRegistry/Facade.hh"

Totem::TaskControllerRegistry::Registry taskRegistry;
Totem::Output::Aggregator aggregator(taskRegistry.hooks());
Totem::Output::OutputUart uart;

void setup() {
    ABORT_IF_ERR_BEGIN(taskRegistry.begin());

    ABORT_IF_ERR_BEGIN(uart.begin());
    ABORT_IF_UNEXPECTED(uartSink, uart.sink(),
                        "Failed to get sink from UART output");

    ABORT_IF_ERR_BEGIN(aggregator.begin());
    ABORT_IF_ERR(aggregator.addSink(uartSink),
                 "Failed to add UART sink to aggregator");

    ABORT_IF_ERR(Logger::setBackend(aggregator),
                 "Failed to set logger backend to aggregator");

    _log_i("Setup complete");

    _log_d("This is a debug message");
    _log_i("This is an info message");
    _log_w("This is a warning message");
    _log_e("This is an error message");
}

extern "C" {
void app_main(void);
}

TickType_t lastWakeTime;

void app_main() {
    setup();
    for (;;) {
        xTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(100));
    }
}
