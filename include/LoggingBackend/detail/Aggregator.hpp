#pragma once

#include "Base/HasCommands.hpp"
#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "LoggingBackend/Interfaces/Config.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include "LoggingBackend/detail/Commands.hpp"
#include "LoggingBackend/detail/HasLogLevel.hpp"
#include "LoggingBackend/detail/IRecordSink.hpp"
#include "LoggingBackend/detail/Metrics.hpp"
#include "LoggingBackend/detail/Types.hpp"
#include "Macros/Facade.hpp"
#include "Macros/internal/Markers.hpp"
#include "Platform/PlatformSelect.hpp"
#include "RingBuffer/Facade.hpp"
#include "Services/Logging.hpp"
#include "StaticConfig/Logging.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <type_traits>

namespace Totem::LoggingBackend::detail {

class BINDING Aggregator : public HasLifecycle<Aggregator, AggregatorConfig>,
                           HasTaskController<Aggregator, AggregatorConfig>,
                           public HasCommands<Aggregator, Commands<Aggregator>>,
                           public HasLogLevel,
                           public ILogger {
    friend class HasLifecycle<Aggregator, AggregatorConfig>;
    friend struct LifecycleContract<Aggregator, AggregatorConfig>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<Aggregator, AggregatorConfig>;
    friend struct TaskController::TaskHooks::Contract<Aggregator>;
    friend struct TaskControllerContract<Aggregator>;

  public:
    explicit Aggregator(TaskController::IRegistry &registry)
        : HasTaskController(registry), HasLogLevel(name) {}

    DELETE_COPY(Aggregator)
    DELETE_MOVE(Aggregator)

    static constexpr const char *name = "Aggregator";

    [[nodiscard]] bool loggingFor(
        LogLevel level,
        std::optional<LogComponent> component = std::nullopt) const override {
        return HasLogLevel::loggingFor(level, component);
    }

    ReturnCode
    setLogLevel(LogLevel level,
                std::optional<LogComponent> component = std::nullopt) override {
        return HasLogLevel::setLogLevel(level, component);
    }

    ReturnCode setComponentLogLevelDefault(LogComponent component) override {
        return HasLogLevel::setComponentLogLevelDefault(component);
    }

    ReturnCode send(const LogRecord &record) override {
        auto size = sizeof(record);
        auto ret = RingBuffer::Buffer::send(_ringBuffer, &record, size,
                                            config().sendTimeoutMs);
        if (!ret) {
            if (ret == ERR(Timeout)) {
                ++droppedRecords;
                return OK();
            }
            ++droppedRecords;
            return ret;
        }
        ++processedRecords;
        return OK();
    }

    ReturnCode addSink(IRecordSink &sink) {
        for (size_t i = 0; i < LoggingConfig::maxSinks; ++i) {
            if (_sinks[i] == nullptr) {
                _sinks[i] = &sink;
                return OK();
            }
        }
        FAIL(ERR(OutOfMemory), "No available slots for sink in %s", name);
    }

  private:
    ReturnCode _onBegin() {
        FAIL_IF_ERR_FWD(_registerCommands(),
                        "Failed to register commands for %s", name);

        DEFAULT_TASK();

        auto ringBufferResult = _createRingBuffer();
        FAIL_IF_UNEXPECTED(ringBuffer, ringBufferResult,
                           ringBufferResult.error(),
                           "Failed to create ring buffer for %s", name);
        _ringBuffer = ringBuffer;

        START_TASK();

        return OK();
    }

    std::expected<::platform::RingBufferHandle, ReturnCode> _createRingBuffer() {
        auto size = config().ringBufferSize * sizeof(LogRecord);
        if (config().ringBufferAllocation == RingBufferAllocation::Dynamic) {
            return RingBuffer::Buffer::create(size);
        }

        if constexpr (AggregatorConfig::hasStaticRingBufferStorage) {
            return RingBuffer::Buffer::create(_ringBufferStorage, size);
        }
        _log_e("Static ring buffer storage is disabled for %s", name);
        return std::unexpected(ERR(InvalidArgument));
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
        _metrics.setProcessed(processedRecords);
        _metrics.setDropped(droppedRecords);
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
            if (sink == nullptr) {
                continue;
            }
            if (!sink->loggingFor(data->level, data->component)) {
                continue;
            }
            auto ret = sink->write(*data);
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

    struct EmptyRingBufferStorage {};
    using RingBufferStorage = std::conditional_t<
        AggregatorConfig::hasStaticRingBufferStorage,
        RingBuffer::Buffer::Storage<AggregatorConfig::maxRingBufferSize *
                                    sizeof(LogRecord)>,
        EmptyRingBufferStorage>;

    RingBufferStorage _ringBufferStorage{};
    ::platform::RingBufferHandle _ringBuffer = nullptr;
    std::array<IRecordSink *, LoggingConfig::maxSinks> _sinks{nullptr};

    uint32_t droppedRecords = 0;
    uint32_t processedRecords = 0;

    Metrics _metrics = Metrics::create();

    static constexpr LogComponent logComponent =
        Totem::LoggingBackend::detail::logComponent;
};

inline constexpr LifecycleContract<Aggregator, AggregatorConfig>
    _aggregator_lifecycle;
inline constexpr TaskControllerContract<Aggregator> _aggregator_task_controller;
inline constexpr RingBuffer::Contract<LogRecord> _aggregator_ring_buffer;
inline constexpr TaskController::TaskHooks::Contract<Aggregator>
    _aggregator_task_hook;

} // namespace Totem::LoggingBackend::detail
