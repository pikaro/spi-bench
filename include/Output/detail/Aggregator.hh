#pragma once

#include "Base/HasLifecycle.hh"
#include "Base/HasTaskController.hh"
#include "Common.hh"

#include "Output/detail/Config.hh"
#include "Output/detail/PlatformSelect.hh"
#include "Output/detail/Types.hh"
#include "RingBuffer/Facade.hh"

namespace Totem::Output::detail {

class Aggregator : public HasLifecycle<Aggregator, AggregatorConfig>,
                   HasTaskController<Aggregator, AggregatorConfig> {
    friend class HasLifecycle<Aggregator, AggregatorConfig>;
    friend struct LifecycleContract<Aggregator, AggregatorConfig>;

    friend TaskController::TaskHooks;
    friend struct TaskController::TaskHooks::Contract<Aggregator>;

  public:
    explicit Aggregator(TaskController::RegistryHooks registryHooks)
        : HasTaskController(registryHooks) {}

    static constexpr const char *name = "Output::Aggregator";

    ReturnCode setLogLevel(LogLevel level) {
        _logLevel = level;
        return OK();
    }

    ReturnCode send(const LogRecord &record) {
        auto size = sizeof(record);
        auto result = RingBuffer::Buffer::send(_ringBuffer, &record, size,
                                               config().sendTimeoutMs);
        FAIL_IF_ERR_FWD(result, "Failed to send message to ring buffer in %s",
                        name);
        return OK();
    }

    ReturnCode addSink(const Sink &sink) {
        for (size_t i = 0; i < AggregatorConfig::maxSinks; ++i) {
            if (!_sinks[i].validate()) {
                _sinks[i] = sink;
                return OK();
            }
        }
        return ERR(CoreError, OutOfMemory);
    }

  private:
    ReturnCode _onBegin() {
        auto taskHooks = TaskController::TaskHooks::bind(*this);

        FAIL_IF_ERR_FWD(_beginTaskController(),
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
        FAIL_IF_ERR_FWD(_endTaskController(),
                        "Failed to end task controller for %s", name);
        FAIL_IF_ERR_FWD(RingBuffer::Buffer::destroy(_ringBuffer),
                        "Failed to destroy ring buffer for %s", name);
        return OK();
    }

    ReturnCode _onTaskStep() {
        while (true) {
            auto result = _receiveRecord();
            if (!result.ok()) {
                if (result == ERR(Timeout)) {
                    break;
                }
                FAIL_IF_ERR_FWD(result, "Failed to receive record in %s", name);
            }
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
            if (data->level >= _logLevel) {
                auto ret = sink.write(*data);
                if (!ret.ok()) {
                    writeResult = ret;
                }
            }
        }
        auto returnResult =
            RingBuffer::Buffer::returnItem(_ringBuffer, (void *)data);
        FAIL_IF_ERR_FWD(returnResult,
                        "Failed to return item to ring buffer in %s", name);
        return writeResult;
    }

    RingBufferHandle _ringBuffer;
    LogLevel _logLevel = LogLevel::Info;
    std::array<Sink, AggregatorConfig::maxSinks> _sinks{};

    using DefaultError = CoreError;
};

inline constexpr LifecycleContract<Aggregator, AggregatorConfig>
    _aggregator_lifecycle;
inline constexpr TaskControllerContract<Aggregator> _aggregator_task_controller;
inline constexpr RingBuffer::Contract<LogRecord> _aggregator_ring_buffer;
inline constexpr TaskController::TaskHooks::Contract<Aggregator>
    _aggregator_task_hook;

} // namespace Totem::Output::detail
