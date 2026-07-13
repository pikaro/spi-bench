#pragma once

#include "CommandBackend/Facade.hpp"
#include "FileSystem/detail/Commands.hpp"
#include "Generic/ConditionalMember.hpp"
#include "LoggingBackend/Facade.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Facade.hpp"
#include "Monitoring/Facade.hpp"
#include "Mutex/detail/Metrics.hpp"
#include "Platform/Console.hpp"
#include "Platform/PlatformSelect.hpp"
#include "SecretStorage/Facade.hpp"
#include "Services/Commands.hpp"
#include "Services/FileSystem.hpp"
#include "Services/Metrics.hpp"
#include "Services/Secret.hpp"
#include "Services/StatusLed.hpp"
#include "StaticConfig/Logging.hpp"
#include "StatusLed/Facade.hpp"
#include "Support/CoreCommands.hpp"
#include "TaskControllerRegistry/Facade.hpp"
#include "Types/Error.hpp"
#include <cstdint>

struct CoreSetup {
    CoreSetup()
        : metricsBinding(metricsBackend), statusLedBinding(statusLed),
          aggregator(taskRegistry), errorJournal(taskRegistry),
          commandController(taskRegistry),
          consoleSource(commandController.catalog(), &commandController,
                        Totem::CommandBackend::Controller::wake),
          systemTaskSource(taskRegistry), monitoring(taskRegistry) {}

    ReturnCode beginStatusLedEarly(const Totem::StatusLed::Config &config) {
        return statusLed.begin(config);
    }

    void setup() {
        ::platform::wait_for_ready();
        ABORT_IF_ERR_BEGIN(::platform::init());

        ABORT_IF_ERR_BEGIN(::platform::Console::init());

        ABORT_IF_ERR_BEGIN(taskRegistry.begin());

        ABORT_IF_ERR_BEGIN(consoleOutput.begin());

        ABORT_IF_ERR(commandController.addTransport(consoleSource),
                     "Failed to add console transport to command controller");
        CommandRegistrarService::set(commandController.registrar());
        CommandCatalogService::set(commandController.catalog());

        ABORT_IF_ERR_BEGIN(metricsBackend.begin());
        Totem::Mutex::detail::prewarmMetrics();

        ABORT_IF_ERR(register_core_commands(),
                     "Failed to register core commands to command controller");

        ABORT_IF_ERR_BEGIN(aggregator.begin());
        ABORT_IF_ERR(aggregator.addSink(consoleOutput),
                     "Failed to add console sink to aggregator");

        LoggingService::set(aggregator);
        ABORT_IF_ERR_BEGIN(Totem::LoggingBackend::NativeLogBridge::begin());

        ABORT_IF_ERR_BEGIN(secretStorage.begin());
        SecretService::set(secretStorage);

        auto fileSystemBegin = fileSystem.begin();
        if (!fileSystemBegin.ok()) {
            _log_e("LittleFS unavailable: " ERR_FMT, ERR_ARG(fileSystemBegin));
        } else {
            FileSystemService::set(fileSystem);
            _setupErrorJournal();
            if (auto registerRet =
                    Totem::FileSystem::detail::registerCommands(fileSystem);
                !registerRet.ok()) {
                _log_e("Failed to register filesystem commands: " ERR_FMT,
                       ERR_ARG(registerRet));
            }
            if (auto listRet = Totem::FileSystem::detail::logFileSystemContents(
                    fileSystem);
                !listRet.ok()) {
                _log_e("Failed to validate LittleFS contents: " ERR_FMT,
                       ERR_ARG(listRet));
            }
        }

        ABORT_IF_ERR_BEGIN(monitoring.begin());
        ABORT_IF_ERR_BEGIN(commandController.begin());
        ABORT_IF_ERR_BEGIN(consoleSource.begin());

        _log_i("Core setup complete");
        ABORT_IF_ERR(StatusLedService::setCoreReady(),
                     "Failed to set status LED core-ready state");
    }

    ReturnCode work(uint32_t nowMs) {
        auto ret = OK();
        ret.combine(StatusLedService::work(nowMs));
        if (auto reapResult = taskRegistry.reap(); !reapResult.ok()) {
            _log_e("Error during task registry reap: " ERR_FMT,
                   ERR_ARG(reapResult));
            ret.combine(reapResult);
        }
        return ret;
    }

    void _setupErrorJournal() {
        if constexpr (LoggingConfig::errorJournalEnabled) {
            auto &journal = errorJournal.get();
            if (auto journalBegin = journal.begin(); !journalBegin.ok()) {
                return;
            }
            if (auto sinkRet = aggregator.addSink(journal); !sinkRet.ok()) {
                (void)journal.end();
            }
        }
    }

    struct MetricsBinding {
        explicit MetricsBinding(Totem::MetricsBackend::Backend &backend) {
            MetricsService::set(backend);
            MetricsService::setRegistrar(backend.registrar());
            MetricsService::setRecorder(backend.recorder());
        }
    };

    struct StatusLedBinding {
        explicit StatusLedBinding(Totem::StatusLed::detail::IService &backend) {
            StatusLedService::set(backend);
        }
    };

    Totem::MetricsBackend::Backend metricsBackend;
    MetricsBinding metricsBinding;
    Totem::StatusLed::StatusLed statusLed;
    StatusLedBinding statusLedBinding;
    Totem::TaskControllerRegistry::Registry taskRegistry;
    Totem::LoggingBackend::Aggregator aggregator;
    Totem::LoggingBackend::ConsoleSink consoleOutput;
    [[no_unique_address]] ConditionalMember<
        LoggingConfig::errorJournalEnabled,
        Totem::LoggingBackend::ErrorJournalSink> errorJournal;
    Totem::CommandBackend::Controller commandController;
    Totem::CommandBackend::ConsoleTransport consoleSource;
    Totem::TaskControllerRegistry::SystemTaskSource systemTaskSource;
    Totem::Monitoring::Monitoring monitoring;
    Totem::SecretStorage::Storage secretStorage;
    FileSystemService::DefaultFileSystem fileSystem;
};
