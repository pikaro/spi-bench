#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "Concepts/Base.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Platform/platform/PlatformESP32/Uart.hpp"
#include "Queue/Facade.hpp"
#include "StaticConfig/UartNode.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "Types/Error.hpp"
#include "Types/Signal.hpp"
#include "Types/Uart.hpp"
#include "Wire/Interfaces/Request.hpp"
#include "Wire/Rs485/detail/Transceiver.hpp"
#include "Wire/Rs485/detail/Types.hpp"
#include <atomic>
#include <concepts>
#include <cstdint>

namespace Totem::Wire::Rs485::detail {

template <class Derived, typename ConfT>
class Node : public HasLifecycle<Derived, ConfT>,
             public HasTaskController<Derived, ConfT> {

    struct RxQueueItem {
        ReadRequestHandle handle;
        ReadRequest request;
    };

    struct TxQueueItem {
        WriteRequestHandle handle;
        WriteRequest request;
    };

    static_assert(
        requires(ConfT &obj) {
            { obj.uartConfig } -> std::same_as<UartConfig &>;
        }, "Node type must have a uartConfig member of type UartConfig");

  public:
    DELETE_COPY(Node)
    DELETE_MOVE(Node)

    explicit Node(TaskController::IRegistry &registry)
        : HasTaskController<Derived, ConfT>(registry),
          _transceiver(this->name, this->_transceiverMode) {}

    ReturnCode send(const WriteRequest &request) {
        FAIL_IF_NOT(ready(), ERR(CoreError, InvalidState),
                    "Cannot send data before node is synced in %s", this->name);
        FAIL_IF_ERR_FWD(_enqueueSend(request),
                        "Failed to send write request for %s", this->name);
        return OK();
    }

    ReturnCode read(ReadRequest &request) {
        FAIL_IF_NOT(ready(), ERR(CoreError, InvalidState),
                    "Cannot read data before node is synced in %s", this->name);
        FAIL_IF_ERR_FWD(_enqueueRead(request),
                        "Failed to send read request for %s", this->name);
        return OK();
    }

    [[nodiscard]] bool ready() const {
        return static_cast<uint8_t>(_state.load(std::memory_order_acquire)) >=
               static_cast<uint8_t>(NodeState::Synced);
    }

  protected:
    ReturnCode _onBegin() {
        _log_i("RS485 node initialized on UART %u with baud rate %u",
               this->config().uartConfig.uartNumber,
               static_cast<uint32_t>(this->config().uartConfig.baudRate));

        auto taskHooks = TaskController::TaskHooks::bind(*this);

        FAIL_IF_ERR_FWD(_beginTaskController(this->config().task),
                        "Failed to begin task controller for %s", this->name);

        auto taskAddResult =
            this->_taskController.addTask(this->config().task.name, taskHooks);
        FAIL_IF_UNEXPECTED(task, taskAddResult, taskAddResult.error(),
                           "Failed to bind task hooks for %s", this->name);

        auto rxQueueResult = Totem::Queue::Platform::create(_rxQueueStorage);
        FAIL_IF_UNEXPECTED_FWD(rxQueue, rxQueueResult,
                               "Failed to create Rx queue: " ERR_FMT,
                               ERR_ARG(rxQueueResult.error()));
        _rxQueue = rxQueue;

        auto txQueueResult = Totem::Queue::Platform::create(_txQueueStorage);
        FAIL_IF_UNEXPECTED_FWD(txQueue, txQueueResult,
                               "Failed to create Tx queue: " ERR_FMT,
                               ERR_ARG(txQueueResult.error()));
        _txQueue = txQueue;

        FAIL_IF_ERR_FWD(_transceiver.init(this->config().uartConfig),
                        "Failed to initialize transceiver for %s", this->name);

        FAIL_IF_ERR_FWD(
            this->_taskController.startTask(task, this->config().task),
            "Failed to start task for %s", this->name);
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();

        if (auto result =
                ::platform::Uart::deinit(this->config().uartConfig.uartNumber);
            !result.ok()) {
            _log_e("Failed to deinitialize UART for RS485 node: " ERR_FMT,
                   ERR_ARG(result));
            ret.combine(result);
        }

        if (_rxQueue != nullptr) {
            ret.combine(Totem::Queue::Platform::destroy(_rxQueue));
            _rxQueue = nullptr;
        }

        if (_txQueue != nullptr) {
            ret.combine(Totem::Queue::Platform::destroy(_txQueue));
            _txQueue = nullptr;
        }

        if (auto result = this->_transceiver.deinit(); !result.ok()) {
            _log_e("Failed to deinitialize transceiver for %s: " ERR_FMT,
                   this->name, ERR_ARG(result));
            ret.combine(result);
        }

        return ret;
    }

    ReturnCode _enqueueSend(const WriteRequest &request) {
        FAIL_IF_INACTIVE_ERR("Uart not initialized for writing in %s",
                             this->name);
        FAIL_IF(!request.validate(), ERR(CoreError, InvalidArgument),
                "Invalid write request");
        _log_d("Sending frame with payload length %u", request.data.size());
        FAIL_IF_ERR_FWD(Totem::Queue::Platform::send(_txQueue, &request),
                        "Failed to send write request to Tx queue for %s",
                        this->name);
        FAIL_IF_ERR_FWD(_wake(),
                        "Failed to wake task for new data in Tx queue for %s",
                        this->name);
    }

    ReturnCode _enqueueRead(ReadRequest &request) {
        FAIL_IF_INACTIVE_ERR("Uart not initialized for reading in %s",
                             this->name);
        FAIL_IF(!request.validate(), ERR(CoreError, InvalidArgument),
                "Invalid read request");
        _log_d("Reading frame with max payload length %u", request.data.size());
        FAIL_IF_ERR_FWD(Totem::Queue::Platform::send(_rxQueue, &request),
                        "Failed to send read request to Rx queue for %s",
                        this->name);
        FAIL_IF_ERR_FWD(_wake(),
                        "Failed to wake task for new data in Rx queue for %s",
                        this->name);
    }

    ReturnCode _wake() {
        FAIL_IF_ERR_FWD(this->_taskController.notifyTask(_task, Signal::Ping),
                        "Failed to notify task of new data in for %s",
                        this->name);
        return OK();
    }

    static ReturnCode _ignoreWriteCallback(const WriteResult &result) {
        auto *self = static_cast<Derived *>(result.owner);
        return self->_onWriteComplete(result);
    }

    ReturnCode _ignoreOnWriteComplete(const WriteResult &result) {
        FAIL_IF_ERR_FWD(result.result, "Write request failed for %s",
                        this->name);
        _log_d("Write request completed successfully, bytes written: %u",
               result.length);
        return OK();
    }

    static ReturnCode _readCallback(const ReadResult &result) {
        auto *self = static_cast<Derived *>(result.owner);
        return self->_onReadComplete(result);
    }

    static ReturnCode _onTaskNotify(Signal /*signal*/) { return OK(); }

    static const LogComponent logComponent =
        Totem::Wire::Rs485::detail::logComponent;

  private:
    platform::TaskHandle _task = nullptr;

    std::atomic<NodeState> _state{NodeState::Initial};

    Totem::Queue::Platform::Storage<RxQueueItem, UartNodeConfig::rxQueueSize>
        _rxQueueStorage;
    Totem::Queue::Handle _rxQueue = nullptr;

    Totem::Queue::Platform::Storage<TxQueueItem, UartNodeConfig::txQueueSize>
        _txQueueStorage;
    Totem::Queue::Handle _txQueue = nullptr;

    Transceiver _transceiver;
};

template <class Derived> struct NodeContract {
    static_assert(IsNamedEntity<Derived>,
                  "Derived type must be a named entity");
    static_assert(
        requires(Derived &obj) {
            { obj._transceiverMode } -> std::same_as<TransceiverMode>;
        }, "Derived type must have a _transceiverMode");
};

} // namespace Totem::Wire::Rs485::detail
