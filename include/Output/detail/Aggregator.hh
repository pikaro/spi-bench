#pragma once

#include "Base/HasCommands.hh"
#include "Base/HasLifecycle.hh"
#include "Base/HasTaskController.hh"
#include "Macros/Facade.hh"
#include "Output/Interfaces/Config.hh"
#include "Output/Interfaces/Sink.hh"
#include "Output/detail/Commands.hh"
#include "Output/detail/Types.hh"
#include "RingBuffer/Facade.hh"
#include "StaticConfig/Logging.hh"
#include "TaskController/Facade.hh"
#include "TaskController/Interfaces/RegistryHooks.hh"
#include "TaskController/Interfaces/TaskHooks.hh"
#include "Types/Error.hh"
#include "Types/Logging.hh"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Totem::Output::detail {

class Aggregator : public HasLifecycle<Aggregator, AggregatorConfig>,
                   HasTaskController<Aggregator, AggregatorConfig>,
                   public HasCommands<Aggregator, Commands<Aggregator>> {
    friend class HasLifecycle<Aggregator, AggregatorConfig>;
    friend struct LifecycleContract<Aggregator, AggregatorConfig>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<Aggregator, AggregatorConfig>;
    friend struct TaskController::TaskHooks::Contract<Aggregator>;
    friend struct TaskControllerContract<Aggregator>;

  public:
    explicit Aggregator(TaskController::RegistryHooks registryHooks)
        : HasTaskController(registryHooks) {}

    DELETE_COPY(Aggregator)
    DELETE_MOVE(Aggregator)

    static constexpr const char *name = "Aggregator";

    [[nodiscard]] bool
    loggingFor(LogLevel level,
               std::optional<LogComponent> component = std::nullopt) const {
        if (!component) {
            return level >= _logLevel;
        }
        if (static_cast<uint8_t>(*component) >= _componentLogLevels.size()) {
            _log_e("Invalid log component %u in %s",
                   static_cast<uint8_t>(*component), name);
            return true;
        }
        auto levelNum = static_cast<uint8_t>(level);
        auto componentLevel = static_cast<uint8_t>(
            _componentLogLevels[static_cast<size_t>(*component)].value_or(
                _logLevel));
        return levelNum >= componentLevel;
    }

    ReturnCode
    setLogLevel(LogLevel level,
                std::optional<LogComponent> component = std::nullopt) {
        if (component) {
            if (static_cast<uint8_t>(*component) >=
                _componentLogLevels.size()) {
                return ERR(InvalidArgument);
            }
            _componentLogLevels[static_cast<size_t>(*component)] = level;
            _log_i("Set log level for component " SV_FMT " to " SV_FMT,
                   MAGIC_SV_ARG(*component), MAGIC_SV_ARG(level));
        } else {
            _logLevel = level;
            _log_i("Set default log level to " SV_FMT, MAGIC_SV_ARG(level));
        }
        return OK();
    }

    ReturnCode setComponentLogLevelDefault(LogComponent component) {
        if (static_cast<uint8_t>(component) >= _componentLogLevels.size()) {
            return ERR(InvalidArgument);
        }
        _componentLogLevels[static_cast<size_t>(component)] = std::nullopt;
        return OK();
    }

    ReturnCode send(const LogRecord &record) {
        auto size = sizeof(record);
        return RingBuffer::Buffer::send(_ringBuffer, &record, size,
                                        config().sendTimeoutMs);
    }

    ReturnCode addSink(const Sink &sink) {
        FAIL_IF(!sink.validate(), ERR(InvalidArgument),
                "Invalid sink provided to %s", name);
        for (size_t i = 0; i < LoggingConfig::maxSinks; ++i) {
            if (!_sinks[i].validate()) {
                _sinks[i] = sink;
                return OK();
            }
        }
        FAIL(ERR(OutOfMemory), "No available slots for sink in %s", name);
    }

  private:
    ReturnCode _onBegin() {
        FAIL_IF_ERR_FWD(_registerCommands(),
                        "Failed to register commands for %s", name);

        auto taskHooks = TaskController::TaskHooks::bind(*this);
        _logLevel = config().defaultLogLevel;

        FAIL_IF_ERR_FWD(_beginTaskController(config().task),
                        "Failed to begin task controller for %s", name);

        auto taskAddResult =
            _taskController.addTask("AggregatorTask", taskHooks);
        FAIL_IF_UNEXPECTED(task, taskAddResult, taskAddResult.error(),
                           "Failed to bind task hooks for %s", name);

        auto ringBufferResult = RingBuffer::Buffer::create(
            config().ringBufferSize * sizeof(LogRecord));
        FAIL_IF_UNEXPECTED(ringBuffer, ringBufferResult,
                           ringBufferResult.error(),
                           "Failed to create ring buffer for %s", name);
        _ringBuffer = ringBuffer;

        FAIL_IF_ERR_FWD(_taskController.startTask(task, config().task),
                        "Failed to start task for %s", name);

        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        if (auto err = _deregisterCommands(); !err.ok()) {
            ret = err;
            _log_e("Failed to deregister commands for %s", name);
        }
        if (auto err = _endTaskController(); !err.ok()) {
            ret = err;
            _log_e("Failed to end task controller for %s", name);
        }
        if (auto err = RingBuffer::Buffer::destroy(_ringBuffer); !err.ok()) {
            ret = err;
            _log_e("Failed to destroy ring buffer for %s", name);
        }
        return ret;
    }

    ReturnCode _onTaskStep() {
        size_t drained = 0;
        while (drained < config().ringBufferSize) {
            auto result = _receiveRecord();
            if (!result.ok()) {
                if (result == ERR(Timeout)) {
                    break;
                }
                FAIL_IF_ERR_FWD(result, "Failed to receive record in %s", name);
            }
            ++drained;
        }
        return OK();
    }

    ReturnCode _receiveRecord() {
        auto result = RingBuffer::Buffer::receive<LogRecord>(
            _ringBuffer, config().receiveTimeoutMs);

        if (!result && result.error() == ERR(Timeout)) {
            return ERR(Timeout);
        }

        FAIL_IF(!result, result.error(),
                "Failed to receive message from ring buffer in %s", name);

        const auto *data = result.value().first;
        FAIL_IF_NULL(data, ERR(InvalidData),
                     "Received null data from ring buffer in %s", name);

        ReturnCode writeResult = OK();
        for (const auto &sink : _sinks) {
            auto ret = sink.write(*data);
            if (!ret.ok()) {
                writeResult = ret;
            }
        }

        auto returnResult =
            RingBuffer::Buffer::returnItem(_ringBuffer, (void *)data);
        FAIL_IF_ERR_FWD(returnResult,
                        "Failed to return item to ring buffer in %s", name);
        return writeResult;
    }

    ::platform::RingBufferHandle _ringBuffer;
    LogLevel _logLevel = LogLevel::Info;
    std::array<std::optional<LogLevel>, magic_enum::enum_count<LogComponent>()>
        _componentLogLevels{std::nullopt};
    std::array<Sink, LoggingConfig::maxSinks> _sinks{};

    static const LogComponent logComponent =
        Totem::Output::detail::logComponent;
};

inline constexpr LifecycleContract<Aggregator, AggregatorConfig>
    _aggregator_lifecycle;
inline constexpr TaskControllerContract<Aggregator> _aggregator_task_controller;
inline constexpr RingBuffer::Contract<LogRecord> _aggregator_ring_buffer;
inline constexpr TaskController::TaskHooks::Contract<Aggregator>
    _aggregator_task_hook;

} // namespace Totem::Output::detail
