// IWYU pragma: private

#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "PubSubEventProducer/Interfaces/Config.hpp"
#include "PubSubEventProducer/detail/Metrics.hpp"
#include "PubSubEventProducer/detail/Types.hpp"
#include "Queue/Facade.hpp"
#include "Services/PubSub.hpp"
#include "StaticConfig/PubSubEventProducer.hpp"
#include "TaskController/Facade.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "TaskController/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include "Types/Signal.hpp"
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

namespace Totem::PubSubEventProducer::detail {

class Producer : public HasLifecycle<Producer, Config>,
                 public HasTaskController<Producer, Config> {
    friend class HasLifecycle<Producer, Config>;
    friend struct LifecycleContract<Producer, Config>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<Producer, Config>;
    friend struct TaskController::TaskHooks::Contract<Producer>;
    friend struct TaskControllerContract<Producer>;

    struct Request {
        using Produce = ReturnCode (*)(const Request &request);

        NodeData::PubSub::Topic topic = NodeData::PubSub::Topic::None;
        Produce produce = nullptr;
        std::array<std::byte, PubSubEventProducerConfig::maxFactorySize>
            factory{};
        std::array<std::byte, PubSubEventProducerConfig::maxArgumentSize>
            argument{};
        bool requireSyncedClock = false;
    };

  public:
    explicit Producer(TaskController::IRegistry &registry)
        : HasTaskController<Producer, Config>(registry) {}

    DELETE_COPY(Producer)
    DELETE_MOVE(Producer)

    static constexpr const char *name = "PubSubEventProducer";

    /**
     * Create an ISR-safe callback which queues a compact factory invocation.
     * The factory itself and its return value are evaluated only by the
     * producer task. Captures must therefore contain event identity/config,
     * not references whose lifetime may end while a request is queued.
     */
    template <typename Factory>
        requires std::invocable<std::decay_t<Factory> &>
    [[nodiscard]] auto makeCallback(NodeData::PubSub::Topic topic,
                                    Factory &&factory,
                                    bool requireSyncedClock = false) {
        using StoredFactory = std::decay_t<Factory>;
        _validateFactory<StoredFactory>();

        return [this, topic, requireSyncedClock,
                factory = StoredFactory{std::forward<Factory>(factory)}]() {
            return _enqueue(topic, requireSyncedClock, factory);
        };
    }

    /**
     * Create an ISR-safe callback with one compact, trivially-copyable input.
     * This is intended for state machines which pass a direction or state enum
     * to a factory that constructs the full PubSub event in task context.
     */
    template <typename Input, typename Factory>
        requires std::invocable<std::decay_t<Factory> &, Input>
    [[nodiscard]] auto makeCallback(NodeData::PubSub::Topic topic,
                                    Factory &&factory,
                                    bool requireSyncedClock = false) {
        using StoredFactory = std::decay_t<Factory>;
        using StoredInput = std::decay_t<Input>;
        _validateFactory<StoredFactory>();
        static_assert(std::is_trivially_copyable_v<StoredInput>,
                      "Event callback input must be trivially copyable");
        static_assert(
            sizeof(StoredInput) <= PubSubEventProducerConfig::maxArgumentSize,
            "Event callback input exceeds the ISR queue argument limit");

        return [this, topic, requireSyncedClock,
                factory = StoredFactory{std::forward<Factory>(factory)}](
                   StoredInput input) {
            return _enqueue(topic, requireSyncedClock, factory, input);
        };
    }

  private:
    ReturnCode _onBegin() {
        prewarmMetrics();
        FAIL_IF_NOT(PubSubService::configured(), ERR(InvalidState),
                    "PubSub must be configured before %s", name);

        DEFAULT_TASK();
        _task = task;
        INIT_QUEUE_OR_FAIL(_eventQueue);
        START_TASK();
        return OK();
    }

    static ReturnCode _onTaskNotify(Signal /*signal*/) { return OK(); }

    ReturnCode _onEnd() {
        auto ret = OK();
        _flushRequestMetrics();
        ret.combine(this->_endTaskController());
        DESTROY_QUEUE(ret, _eventQueue);
        _task = 0;
        return ret;
    }

    ReturnCode _onTaskStep() {
        auto ret = OK();
        _flushRequestMetrics();

        Request request{};
        while (Totem::Queue::Platform::receive(_eventQueue, &request, 0).ok()) {
            if (request.produce == nullptr) {
                metrics().addPublishFailure();
                ret.combine(ERR(InvalidData));
                continue;
            }

            auto produceRet = request.produce(request);
            if (!produceRet.ok()) {
                metrics().addPublishFailure();
                _log_w("Failed to produce PubSub event for topic " SV_FMT
                       ": " ERR_FMT,
                       MAGIC_SV_ARG(request.topic), ERR_ARG(produceRet));
                ret.combine(produceRet);
                continue;
            }
            metrics().addPublished();
        }
        return ret;
    }

    template <typename Factory> static consteval void _validateFactory() {
        static_assert(std::is_trivially_copyable_v<Factory>,
                      "Event factory must be trivially copyable");
        static_assert(
            sizeof(Factory) <= PubSubEventProducerConfig::maxFactorySize,
            "Event factory capture exceeds the ISR queue factory limit");
    }

    template <typename Value, size_t StorageSize>
    static void _store(const Value &value,
                       std::array<std::byte, StorageSize> &storage) {
        static_assert(std::is_trivially_copyable_v<Value>);
        static_assert(sizeof(Value) <= StorageSize);
        const auto bytes =
            std::bit_cast<std::array<std::byte, sizeof(Value)>>(value);
        for (size_t i = 0; i < bytes.size(); ++i) {
            storage[i] = bytes[i];
        }
    }

    template <typename Value, size_t StorageSize>
    [[nodiscard]] static Value
    _load(const std::array<std::byte, StorageSize> &storage) {
        static_assert(std::is_trivially_copyable_v<Value>);
        static_assert(sizeof(Value) <= StorageSize);
        std::array<std::byte, sizeof(Value)> bytes{};
        for (size_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = storage[i];
        }
        return std::bit_cast<Value>(bytes);
    }

    template <typename Factory>
    bool _enqueue(NodeData::PubSub::Topic topic, bool requireSyncedClock,
                  const Factory &factory) {
        auto request = Request{
            .topic = topic,
            .produce = _produce<Factory>,
            .requireSyncedClock = requireSyncedClock,
        };
        _store(factory, request.factory);
        return _enqueue(request);
    }

    template <typename Factory, typename Input>
    bool _enqueue(NodeData::PubSub::Topic topic, bool requireSyncedClock,
                  const Factory &factory, const Input &input) {
        auto request = Request{
            .topic = topic,
            .produce = _produce<Factory, Input>,
            .requireSyncedClock = requireSyncedClock,
        };
        _store(factory, request.factory);
        _store(input, request.argument);
        return _enqueue(request);
    }

    bool _enqueue(const Request &request) {
        const bool fromIsr = ::platform::in_isr();
        if (fromIsr) {
            _isrRequests.fetch_add(1, std::memory_order_relaxed);
        } else {
            _taskRequests.fetch_add(1, std::memory_order_relaxed);
        }

        bool queued = false;
        if (_eventQueue != nullptr) {
            queued =
                fromIsr
                    ? Totem::Queue::Platform::sendFromIsr(_eventQueue, &request)
                    : Totem::Queue::Platform::send(_eventQueue, &request, 0)
                          .ok();
        }

        if (!queued) {
            _queueDrops.fetch_add(1, std::memory_order_relaxed);
        } else {
            _enqueued.fetch_add(1, std::memory_order_relaxed);
        }

        if (fromIsr) {
            Totem::TaskController::Controller::signalTaskFromIsr(_task,
                                                                 Signal::Ping);
        } else if (_task != 0) {
            (void)this->_taskController.signalTaskDirect(_task, Signal::Ping);
        }
        return queued;
    }

    template <typename Factory>
    static ReturnCode _produce(const Request &request) {
        auto factory = _load<Factory>(request.factory);
        using Event = std::remove_cvref_t<std::invoke_result_t<Factory &>>;
        return _publish<Event>(std::invoke(factory), request);
    }

    template <typename Factory, typename Input>
    static ReturnCode _produce(const Request &request) {
        auto factory = _load<Factory>(request.factory);
        auto input = _load<Input>(request.argument);
        using Event =
            std::remove_cvref_t<std::invoke_result_t<Factory &, Input>>;
        return _publish<Event>(std::invoke(factory, input), request);
    }

    template <typename Event>
    static ReturnCode _publish(const Event &event, const Request &request) {
        return PubSubService::publish<
            PubSubEventProducerConfig::publishPoolSize>(
            request.topic, event, request.requireSyncedClock);
    }

    void _flushRequestMetrics() {
        const auto queueDrops =
            _queueDrops.exchange(0, std::memory_order_relaxed);
        if (queueDrops != 0) {
            metrics().addQueueDrops(queueDrops);
        }
        const auto enqueued = _enqueued.exchange(0, std::memory_order_relaxed);
        if (enqueued != 0) {
            metrics().addEnqueued(enqueued);
        }
        const auto isrRequests =
            _isrRequests.exchange(0, std::memory_order_relaxed);
        if (isrRequests != 0) {
            metrics().addIsrRequests(isrRequests);
        }
        const auto taskRequests =
            _taskRequests.exchange(0, std::memory_order_relaxed);
        if (taskRequests != 0) {
            metrics().addTaskRequests(taskRequests);
        }
    }

    Totem::TaskController::RunnerKey _task = 0;

    STANDARD_QUEUE(_eventQueue, Request,
                   PubSubEventProducerConfig::eventQueueSize)

    std::atomic<uint32_t> _queueDrops{0};
    std::atomic<uint32_t> _enqueued{0};
    std::atomic<uint32_t> _isrRequests{0};
    std::atomic<uint32_t> _taskRequests{0};

    static constexpr LogComponent logComponent =
        Totem::PubSubEventProducer::detail::logComponent;
};

inline constexpr LifecycleContract<Producer, Config>
    _pubsub_event_producer_lifecycle_contract;
inline constexpr TaskControllerContract<Producer>
    _pubsub_event_producer_task_controller_contract;
inline constexpr TaskController::TaskHooks::Contract<Producer>
    _pubsub_event_producer_task_hooks_contract;

} // namespace Totem::PubSubEventProducer::detail
