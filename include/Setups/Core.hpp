#pragma once

#include "CommandBackend/Facade.hpp"
#include "LoggingBackend/Facade.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Facade.hpp"
#include "Mutex/detail/Metrics.hpp"
#include "Monitoring/Facade.hpp"
#include "Platform/Console.hpp"
#include "Services/Commands.hpp"
#include "Services/Metrics.hpp"
#include "Support/CoreCommands.hpp"
#include "TaskControllerRegistry/Facade.hpp"
#include "Types/Error.hpp"
#include <cstdint>

struct CoreSetup {
    CoreSetup()
        : metricsBinding(metricsBackend), aggregator(taskRegistry),
          commandController(taskRegistry), systemTaskSource(taskRegistry),
          monitoring(taskRegistry) {}

    void setup() {
        ::platform::wait_for_ready();

        ABORT_IF_ERR_BEGIN(::platform::Console::init());

        ABORT_IF_ERR_BEGIN(taskRegistry.begin());

        ABORT_IF_ERR_BEGIN(consoleOutput.begin());

        ABORT_IF_ERR_BEGIN(commandController.begin());
        ABORT_IF_ERR(commandController.addTransport(consoleSource),
                     "Failed to add console transport to command controller");
        CommandRegistrarService::set(commandController.registrar());

        ABORT_IF_ERR_BEGIN(metricsBackend.begin());
        (void)Totem::Mutex::detail::metrics();

        ABORT_IF_ERR(register_core_commands(),
                     "Failed to register core commands to command controller");

        ABORT_IF_ERR_BEGIN(aggregator.begin());
        ABORT_IF_ERR(aggregator.addSink(consoleOutput),
                     "Failed to add console sink to aggregator");

        LoggingService::set(aggregator);

        ABORT_IF_ERR_BEGIN(monitoring.begin());
    }

    ReturnCode work(uint32_t /*unused*/) {
        auto ret = OK();
        if (auto reapResult = taskRegistry.reap(); !reapResult.ok()) {
            _log_e("Error during task registry reap: " ERR_FMT,
                   ERR_ARG(reapResult));
            ret.combine(reapResult);
        }
        return ret;
    }

    struct MetricsBinding {
        explicit MetricsBinding(Totem::MetricsBackend::Backend &backend) {
            MetricsService::set(backend);
            MetricsService::setRegistrar(backend.registrar());
            MetricsService::setRecorder(backend.recorder());
        }
    };

    Totem::MetricsBackend::Backend metricsBackend;
    MetricsBinding metricsBinding;
    Totem::TaskControllerRegistry::Registry taskRegistry;
    Totem::LoggingBackend::Aggregator aggregator;
    Totem::LoggingBackend::ConsoleOutput consoleOutput;
    Totem::CommandBackend::Controller commandController;
    Totem::CommandBackend::ConsoleTransport consoleSource;
    Totem::TaskControllerRegistry::SystemTaskSource systemTaskSource;
    Totem::Monitoring::Monitoring monitoring;
};
