#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "Concepts/Base.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Queue/Facade.hpp"
#include "StaticConfig/UartNode.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include "Types/Signal.hpp"
#include "Types/Uart.hpp"
#include "Wire/Interfaces/Request.hpp"
#include "Wire/Rs485/detail/AttentionLine.hpp"
#include "Wire/Rs485/detail/Metrics.hpp"
#include "Wire/Rs485/detail/Pdu.hpp"
#include "Wire/Rs485/detail/Trace.hpp"
#include "Wire/Rs485/detail/Transceiver.hpp"
#include "Wire/Rs485/detail/Types.hpp"
#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace Totem::Wire::Rs485::detail {

template <class Derived, typename ConfT, TransceiverMode Mode>
class Node : public HasLifecycle<Derived, ConfT>,
             public HasTaskController<Derived, ConfT> {

    struct TxQueueItem {
        WriteRequestHandle handle;
        WriteRequest request;
    };

    struct ExchangeQueueItem {
        ExchangeRequestHandle handle;
        TransactionKind kind;
        ExchangeRequest request;
    };

    struct RequestWriteContext {
        ExchangeRequest *request = nullptr;
    };

    static_assert(
        requires(ConfT &obj) {
            { obj.uartConfig } -> std::same_as<UartConfig &>;
            { obj.task } -> std::same_as<Totem::TaskController::Config &>;
        }, "Node config must provide uartConfig and task members");

  public:
    DELETE_COPY(Node)
    DELETE_MOVE(Node)

    explicit Node(TaskController::IRegistry &registry)
        : HasTaskController<Derived, ConfT>(registry),
          _transceiver(Derived::name) {}

    ReturnCode send(const WriteRequest &request) {
        FAIL_IF_NOT(ready(), ERR(CoreError, InvalidState),
                    "Cannot send data before node is synced in %s",
                    Derived::name);
        FAIL_IF_ERR_FWD(_enqueueSend(request),
                        "Failed to enqueue write request for %s",
                        Derived::name);
        return OK();
    }

    ReturnCode exchange(const ExchangeRequest &request) {
        FAIL_IF_NOT(ready(), ERR(CoreError, InvalidState),
                    "Cannot exchange before node is synced in %s",
                    Derived::name);
        FAIL_IF_ERR_FWD(_enqueueExchange(TransactionKind::Request, request),
                        "Failed to enqueue exchange request for %s",
                        Derived::name);
        return OK();
    }

    ReturnCode poll(const ExchangeRequest &request) {
        FAIL_IF_NOT(ready(), ERR(CoreError, InvalidState),
                    "Cannot poll before node is synced in %s", Derived::name);
        FAIL_IF_ERR_FWD(_enqueueExchange(TransactionKind::Poll, request),
                        "Failed to enqueue poll request for %s", Derived::name);
        return OK();
    }

    ReturnCode registerHandler(const FrameHandler &handler) {
        FAIL_IF(!handler.validate(), ERR(CoreError, InvalidArgument),
                "Invalid RS485 frame handler registration for %s",
                Derived::name);
        for (auto &slot : _handlers) {
            if (slot.owner == nullptr) {
                slot = handler;
                return OK();
            }
        }
        return ERR(CoreError, OutOfMemory);
    }

    [[nodiscard]] bool ready() const {
        return _state.load(std::memory_order_acquire) == NodeState::Synced;
    }

  protected:
    int64_t _beginTaskStep() {
        metrics().addTaskStep();
        if constexpr (Metrics::profilingEnabled) {
            return ::platform::get_time_us();
        }
        return 0;
    }

    void _endTaskStep(int64_t startedAtUs) {
        if constexpr (Metrics::profilingEnabled) {
            _finishTaskStep(startedAtUs);
        }
    }

    ReturnCode _onBegin() {
        prewarmMetrics();

        _log_i("RS485 node initialized on UART %u with baud rate %u",
               this->config().uartConfig.uartNumber,
               static_cast<uint32_t>(this->config().uartConfig.baudRate));

        FAIL_IF(this->config().uartConfig.uartNumber == 0,
                ERR(CoreError, InvalidArgument), "UART number 0 is reserved");

        DEFAULT_TASK(Derived::name);
        _task = task;

        INIT_QUEUE_OR_FAIL(_txQueue);
        INIT_QUEUE_OR_FAIL(_exchangeQueue);

        FAIL_IF_ERR_FWD(_transceiver.init(this->config().uartConfig),
                        "Failed to initialize transceiver for %s",
                        Derived::name);
        FAIL_IF_ERR_FWD(_transceiver.registerUartCallback(this, _onUartEvent),
                        "Failed to register UART event callback for %s",
                        Derived::name);

        START_TASK(Derived::name);

        FAIL_IF_ERR_FWD(_initAttentionLine(),
                        "Failed to initialize RS485 attention line for %s",
                        Derived::name);
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        ret.combine(_attention.deinit());
        _task = 0;

        if (_txQueue != nullptr) {
            ret.combine(Totem::Queue::Platform::destroy(_txQueue));
            _txQueue = nullptr;
        }

        if (_exchangeQueue != nullptr) {
            ret.combine(Totem::Queue::Platform::destroy(_exchangeQueue));
            _exchangeQueue = nullptr;
        }

        if (auto result = _transceiver.deinit(); !result.ok()) {
            _log_e("Failed to deinitialize transceiver for %s: " ERR_FMT,
                   Derived::name, ERR_ARG(result));
            ret.combine(result);
        }

        return ret;
    }

    ReturnCode _runReadyTransactions() {
        if (!ready()) {
            return OK();
        }
        bool returnUnusedSlaveTurn = false;
        if (_transceiver.canRead()) {
            FAIL_IF_ERR_FWD(_pollIncoming(),
                            "Failed to poll incoming frame for %s",
                            Derived::name);
            returnUnusedSlaveTurn =
                !Derived::sendsHeartbeat && _transceiver.canInitiateWrite();
            if (!ready() || !_transceiver.canInitiateWrite()) {
                return OK();
            }
        }
        if (!_transceiver.canInitiateWrite()) {
            return OK();
        }
        FAIL_IF_ERR_FWD(_heartbeatStep(), "Failed heartbeat step for %s",
                        Derived::name);
        if (!ready() || !_transceiver.canInitiateWrite()) {
            return OK();
        }
        FAIL_IF_ERR_FWD(_processOneSend(), "Failed to process send for %s",
                        Derived::name);
        if (!ready() || !_transceiver.canInitiateWrite()) {
            return OK();
        }
        FAIL_IF_ERR_FWD(_processOneExchange(),
                        "Failed to process exchange for %s", Derived::name);
        if (!ready() || !_transceiver.canInitiateWrite()) {
            return OK();
        }
        FAIL_IF_ERR_FWD(_nopStep(returnUnusedSlaveTurn),
                        "Failed to process nop for %s", Derived::name);
        return OK();
    }

    ReturnCode _pollIncoming() {
        metrics().addPoll();
        auto headerResult = _transceiver.pollHeader();
        if (!headerResult) {
            if (headerResult.error() == ERR(CoreError, NotFound)) {
                metrics().addEmptyPoll();
                return OK();
            }
            if (headerResult.error() == ERR(CoreError, InvalidState)) {
                if (ready()) {
                    _resetConnection("turn state mismatch");
                } else {
                    _transceiver.resetTurn();
                }
                return OK();
            }
            if (headerResult.error() == ERR(WireError, Corrupted) ||
                headerResult.error() == ERR(WireError, CrcError)) {
                _log_w("Discarding invalid RS485 input for %s: " ERR_FMT,
                       Derived::name, ERR_ARG(headerResult.error()));
                (void)_transceiver.discardRx();
                _resetConnection("corrupted input");
                return OK();
            }
            return headerResult.error();
        }
        auto header = *headerResult;
        return _handleIncomingHeader(header);
    }

    ReturnCode _handleIncomingHeader(const Header &header) {
        metrics().addReceivedFrame();
        log_trace_packet("rx.header", header, Derived::name);
        if (!ready() && header.type != FrameType::Hello) {
            _log_w("Discarding stale RS485 frame during handshake in %s",
                   Derived::name);
            (void)_transceiver.discardRx();
            _transceiver.resetTurn();
            return OK();
        }
        if (header.type != FrameType::Hello) {
            auto sequenceResult = header.validateSequence();
            if (!sequenceResult.ok()) {
                _log_w("Invalid RS485 sequence in %s; resetting link: " ERR_FMT,
                       Derived::name, ERR_ARG(sequenceResult));
                (void)_transceiver.discardRx();
                _resetConnection("sequence error");
                return OK();
            }
        }

        switch (header.type) {
        case FrameType::Nop:
            return _receiveNop(header);
        case FrameType::Grant:
            return _receiveGrant(header);
        case FrameType::Hello:
            return this->derived()._onHello(header);
        case FrameType::Heartbeat:
            return _receiveHeartbeat(header);
        case FrameType::Data:
            return _receiveData(header);
        case FrameType::Request:
        case FrameType::Poll:
            return _receiveRequest(header);
        case FrameType::Ack:
        case FrameType::Nack:
        case FrameType::Response:
        default:
            return ERR(CoreError, InvalidState);
        }
    }

    ReturnCode _sendHello(uint8_t responseTo = 0,
                          FrameTurn turn = FrameTurn::Initiated) {
        Header sentHeader{};
        FAIL_IF_ERR_FWD(_transceiver.sendControl(FrameType::Hello, responseTo,
                                                 PayloadType::Raw, &sentHeader,
                                                 turn),
                        "Failed to send RS485 hello for %s", Derived::name);
        _lastHelloSequence = sentHeader.sequenceNumber;
        return OK();
    }

    ReturnCode _wake(Signal signal = Signal::Ping) {
        FAIL_IF(_task == 0, ERR(CoreError, InvalidState),
                "Cannot wake unregistered RS485 task for %s", Derived::name);
        return this->_taskController.signalTask(_task, signal);
    }

    ReturnCode _heartbeatStep() {
        const auto nowMs = ::platform::get_time();
        if constexpr (Derived::sendsHeartbeat) {
            if (static_cast<uint32_t>(nowMs - _lastHeartbeatSentMs) <
                heartbeatIntervalMs) {
                return OK();
            }

            if (_heartbeatAwaitingResponse) {
                ++_missedHeartbeats;
                _log_w("Missed RS485 heartbeat %u/%u for %s", _missedHeartbeats,
                       maxMissedHeartbeats, Derived::name);
                if (_missedHeartbeats >= maxMissedHeartbeats) {
                    _resetConnection("heartbeat timeout");
                    return OK();
                }
            }

            Header sentHeader{};
            FAIL_IF_ERR_FWD(
                _transceiver.sendControl(FrameType::Heartbeat, 0,
                                         PayloadType::Raw, &sentHeader),
                "Failed to send RS485 heartbeat for %s", Derived::name);
            _lastHeartbeatSentMs = nowMs;
            _heartbeatSequence = sentHeader.sequenceNumber;
            auto responseResult =
                _receiveSynchronousHeader(transactionResponseTimeoutMs);
            if (!responseResult) {
                _heartbeatAwaitingResponse = true;
                _transceiver.resetTurn();
                return OK();
            }
            auto response = *responseResult;
            log_trace_packet("heartbeat.response", response, Derived::name);
            auto sequenceResult = response.validateSequence();
            if (!sequenceResult.ok() || response.type != FrameType::Heartbeat ||
                response.responseTo != _heartbeatSequence ||
                response.payloadLength != 0) {
                _heartbeatAwaitingResponse = true;
                _transceiver.resetTurn();
                return OK();
            }
            _heartbeatAwaitingResponse = false;
            _missedHeartbeats = 0;
            _lastHeartbeatReceivedMs = nowMs;
        } else {
            if (_lastHeartbeatReceivedMs == 0) {
                return OK();
            }
            if (static_cast<uint32_t>(nowMs - _lastHeartbeatReceivedMs) >=
                heartbeatIntervalMs * (maxMissedHeartbeats + 1)) {
                _resetConnection("heartbeat timeout");
            }
        }
        return OK();
    }

    void _resetConnection(const char *reason) {
        metrics().addReset();
        _log_w("Resetting RS485 connection for %s: %s", Derived::name, reason);
        _state.store(NodeState::Initial, std::memory_order_release);
        (void)_transceiver.discardRx();
        _transceiver.resetTurn();
        _failPendingTransactions(ERR(CoreError, InvalidState));
        _heartbeatAwaitingResponse = false;
        _heartbeatSequence = 0;
        _missedHeartbeats = 0;
        _lastHeartbeatReceivedMs = 0;
        _lastHeartbeatSentMs = 0;
        _lastHelloSequence = 0;
        _lastNopSentMs = 0;
        _attentionRequested.store(false, std::memory_order_release);
        (void)_updateAttentionLine();
    }

    void _deferHeartbeat() {
        _lastHeartbeatSentMs = ::platform::get_time();
        _lastHeartbeatReceivedMs = _lastHeartbeatSentMs;
        _heartbeatAwaitingResponse = false;
        _heartbeatSequence = 0;
        _missedHeartbeats = 0;
    }

    void _resetTurn() { _transceiver.resetTurn(); }

    ReturnCode _onTaskNotify(Signal signal) {
        if (signal == Signal::Rs485Attention) {
            metrics().addAttentionWake();
            _attentionRequested.store(true, std::memory_order_release);
        }
        return OK();
    }

    static ReturnCode _onUartEvent(void *owner, UartEvent event) {
        auto *self = static_cast<Node *>(owner);
        switch (event.type) {
        case UartEventType::Data:
            metrics().addUartDataWake();
            return self->_wake(Signal::UartData);
        case UartEventType::Overflow:
            metrics().addUartOverflowWake();
            return self->_wake(Signal::UartOverflow);
        case UartEventType::Error:
        case UartEventType::Break:
        case UartEventType::Pattern:
        case UartEventType::Unknown:
        default:
            metrics().addUartErrorWake();
            return self->_wake(Signal::UartError);
        }
    }

    static void _onAttentionLine(void *owner, AttentionLineEvent event,
                                 int64_t /*timestampUs*/) {
        auto *self = static_cast<Node *>(owner);
        if (self == nullptr || event != AttentionLineEvent::Asserted) {
            return;
        }
        self->_attentionRequested.store(true, std::memory_order_release);
        self->_wakeFromIsr(Signal::Rs485Attention);
    }

    std::atomic<NodeState> _state{NodeState::Initial};
    uint8_t _lastHelloSequence = 0;

    static constexpr uint32_t heartbeatIntervalMs = 500;
    static constexpr uint32_t nopIntervalMs = 10;
    static constexpr uint32_t transactionResponseTimeoutMs = 50;
    static constexpr uint8_t maxMissedHeartbeats = 3;

    static constexpr LogComponent logComponent =
        Totem::Wire::Rs485::detail::logComponent;

  private:
    ReturnCode _initAttentionLine() {
        if constexpr (Derived::sendsHeartbeat) {
            return _attention.initInput(this->config().attentionPin, this,
                                        _onAttentionLine);
        } else {
            return _attention.initOutput(this->config().attentionPin);
        }
    }

    void _wakeFromIsr(Signal signal = Signal::Ping) {
        if (_task == 0) {
            return;
        }
        this->_taskController.signalTaskFromIsr(_task, signal);
    }

    ReturnCode _receiveNop(const Header &header) {
        FAIL_IF(header.payloadLength != 0, ERR(CoreError, InvalidData),
                "RS485 nop must not carry a payload in %s", Derived::name);
        if (header.responseTo != 0) {
            return OK();
        }
        FAIL_IF_ERR_FWD(_transceiver.sendControl(
                            FrameType::Nop, header.sequenceNumber,
                            PayloadType::Raw, nullptr, FrameTurn::Reaction),
                        "Failed to send RS485 nop response for %s",
                        Derived::name);
        return OK();
    }

    ReturnCode _receiveGrant(const Header &header) {
        FAIL_IF(header.responseTo != 0 || header.payloadLength != 0,
                ERR(CoreError, InvalidData),
                "RS485 grant must not carry a response or payload in %s",
                Derived::name);
        return OK();
    }

    ReturnCode _receiveHeartbeat(const Header &header) {
        FAIL_IF(header.payloadLength != 0, ERR(CoreError, InvalidData),
                "RS485 heartbeat must not carry a payload in %s",
                Derived::name);

        const auto nowMs = ::platform::get_time();
        if (header.responseTo != 0) {
            if constexpr (Derived::sendsHeartbeat) {
                if (!_heartbeatAwaitingResponse ||
                    header.responseTo != _heartbeatSequence) {
                    return ERR(CoreError, InvalidState);
                }
                _heartbeatAwaitingResponse = false;
                _missedHeartbeats = 0;
                _lastHeartbeatReceivedMs = nowMs;
                return OK();
            } else {
                return ERR(CoreError, InvalidState);
            }
        }

        _lastHeartbeatReceivedMs = nowMs;
        if constexpr (!Derived::sendsHeartbeat) {
            FAIL_IF_ERR_FWD(_transceiver.sendControl(
                                FrameType::Heartbeat, header.sequenceNumber,
                                PayloadType::Raw, nullptr, FrameTurn::Reaction),
                            "Failed to send RS485 heartbeat response for %s",
                            Derived::name);
        }
        return OK();
    }

    ReturnCode _enqueueSend(const WriteRequest &request) {
        FAIL_IF_INACTIVE_ERR("UART not initialized for writing in %s",
                             Derived::name);
        FAIL_IF(!request.validate(), ERR(CoreError, InvalidArgument),
                "Invalid write request");
        auto item = TxQueueItem{
            .handle = _nextWriteHandle++,
            .request = request,
        };
        FAIL_IF_ERR_FWD(Totem::Queue::Platform::send(_txQueue, &item),
                        "Failed to send write request to Tx queue for %s",
                        Derived::name);
        FAIL_IF_ERR_FWD(_updateAttentionLine(),
                        "Failed to update RS485 attention line for %s",
                        Derived::name);
        return _wake();
    }

    ReturnCode _enqueueExchange(TransactionKind kind,
                                const ExchangeRequest &request) {
        FAIL_IF_INACTIVE_ERR("UART not initialized for exchange in %s",
                             Derived::name);
        FAIL_IF(!request.validate(), ERR(CoreError, InvalidArgument),
                "Invalid exchange request");
        auto item = ExchangeQueueItem{
            .handle = _nextExchangeHandle++,
            .kind = kind,
            .request = request,
        };
        FAIL_IF_ERR_FWD(
            Totem::Queue::Platform::send(_exchangeQueue, &item),
            "Failed to send exchange request to exchange queue for %s",
            Derived::name);
        FAIL_IF_ERR_FWD(_updateAttentionLine(),
                        "Failed to update RS485 attention line for %s",
                        Derived::name);
        return _wake();
    }

    ReturnCode _processOneSend() {
        if (Totem::Queue::Platform::size(_txQueue) == 0) {
            return OK();
        }
        TxQueueItem item{};
        FAIL_IF_ERR_FWD(Totem::Queue::Platform::receive(_txQueue, &item, 0),
                        "Failed to receive Tx queue item for %s",
                        Derived::name);
        FAIL_IF_ERR_FWD(_updateAttentionLine(),
                        "Failed to update RS485 attention line for %s",
                        Derived::name);

        Header sentHeader{};
        metrics().addTxDataFrame();
        int64_t sentAtUs = 0;
        auto ret = _transceiver.sendFrame(
            FrameType::Data, item.request.payloadType, item.request.data, 0,
            &sentHeader, FrameTurn::Initiated, &sentAtUs);
        log_trace_packet("data.sent", sentHeader, Derived::name);
        if (!ret.ok()) {
            return _failWrite(item, ret, "data send failed");
        }

        auto responseResult =
            _receiveSynchronousHeader(transactionResponseTimeoutMs);
        if (!responseResult) {
            return _failWrite(item, responseResult.error(),
                              "data response read failed");
        }
        auto response = *responseResult;
        log_trace_packet("data.response", response, Derived::name);
        auto sequenceResult = response.validateSequence();
        if (!sequenceResult.ok()) {
            return _failWrite(item, sequenceResult, "data response sequence");
        }
        if (response.responseTo != sentHeader.sequenceNumber ||
            response.payloadType != item.request.payloadType ||
            response.payloadLength != 0) {
            return _failWrite(item, ERR(CoreError, InvalidState),
                              "data response mismatch");
        }
        if (response.type == FrameType::Nack) {
            return item.request.nack(item.handle, ERR(WireError, Nack));
        }
        if (response.type != FrameType::Ack) {
            return _failWrite(item, ERR(CoreError, InvalidState),
                              "unexpected data response");
        }
        return item.request.ack(item.handle,
                                static_cast<uint16_t>(item.request.data.size()),
                                ::platform::get_time_us(), sentAtUs);
    }

    ReturnCode _nopStep(bool force = false) {
        const auto nowMs = ::platform::get_time();
        const bool attentionRequested = _consumeAttentionRequest();
        if constexpr (Derived::sendsHeartbeat) {
            if (!attentionRequested) {
                return OK();
            }
        }
        if (!force && !attentionRequested &&
            static_cast<uint32_t>(nowMs - _lastNopSentMs) < nopIntervalMs) {
            return OK();
        }
        _lastNopSentMs = nowMs;
        if constexpr (Derived::sendsHeartbeat) {
            return _sendTurnGrant();
        }
        return _sendNopExchange();
    }

    ReturnCode _sendTurnGrant() {
        Header sentHeader{};
        metrics().addTxGrant();
        FAIL_IF_ERR_FWD(_transceiver.sendGrant(&sentHeader),
                        "Failed to send RS485 grant for %s", Derived::name);
        log_trace_packet("grant.sent", sentHeader, Derived::name);
        auto headerResult =
            _receiveSynchronousHeader(transactionResponseTimeoutMs);
        if (!headerResult) {
            _transceiver.resetTurn();
            return OK();
        }
        return _handleIncomingHeader(*headerResult);
    }

    ReturnCode _sendNopExchange() {
        Header sentHeader{};
        metrics().addTxNop();
        FAIL_IF_ERR_FWD(_transceiver.sendControl(FrameType::Nop, 0,
                                                 PayloadType::Raw, &sentHeader),
                        "Failed to send RS485 nop for %s", Derived::name);
        auto responseResult =
            _receiveSynchronousHeader(transactionResponseTimeoutMs);
        if (!responseResult) {
            _transceiver.resetTurn();
            return OK();
        }
        auto response = *responseResult;
        log_trace_packet("nop.response", response, Derived::name);
        auto sequenceResult = response.validateSequence();
        if (!sequenceResult.ok() || response.type != FrameType::Nop ||
            response.responseTo != sentHeader.sequenceNumber ||
            response.payloadLength != 0) {
            _transceiver.resetTurn();
            return OK();
        }
        return OK();
    }

    ReturnCode _processOneExchange() {
        if (Totem::Queue::Platform::size(_exchangeQueue) == 0) {
            return OK();
        }
        ExchangeQueueItem item{};
        FAIL_IF_ERR_FWD(
            Totem::Queue::Platform::receive(_exchangeQueue, &item, 0),
            "Failed to receive exchange queue item for %s", Derived::name);
        FAIL_IF_ERR_FWD(_updateAttentionLine(),
                        "Failed to update RS485 attention line for %s",
                        Derived::name);

        Header sentHeader{};
        auto frameType = item.kind == TransactionKind::Poll
                             ? FrameType::Poll
                             : FrameType::Request;
        int64_t sentAtUs = 0;
        auto requestWriteContext = RequestWriteContext{
            .request = &item.request,
        };
        metrics().addTxExchange();
        auto ret = _transceiver.sendFrame(
            frameType, item.request.payloadType, item.request.request, 0,
            &sentHeader, FrameTurn::Initiated, &sentAtUs, &requestWriteContext,
            _beforeRequestWrite);
        log_trace_packet("exchange.sent", sentHeader, Derived::name);
        if (!ret.ok()) {
            return _failExchange(item, ret, "exchange send failed");
        }

        auto responseResult =
            _receiveSynchronousHeader(transactionResponseTimeoutMs);
        if (!responseResult) {
            return _failExchange(item, responseResult.error(),
                                 "exchange response read failed");
        }
        auto response = *responseResult;
        log_trace_packet("exchange.response", response, Derived::name);
        auto sequenceResult = response.validateSequence();
        if (!sequenceResult.ok()) {
            return _failExchange(item, sequenceResult,
                                 "exchange response sequence");
        }
        if (response.responseTo != sentHeader.sequenceNumber ||
            response.payloadType != item.request.payloadType) {
            return _failExchange(item, ERR(CoreError, InvalidState),
                                 "exchange response mismatch");
        }
        if (response.type == FrameType::Nack) {
            return item.request.nack(item.handle, ERR(WireError, Nack));
        }
        if (response.type != FrameType::Response) {
            return _failExchange(item, ERR(CoreError, InvalidState),
                                 "unexpected exchange response");
        }

        auto payloadResult =
            _receiveSynchronousPayload(response, item.request.response);
        if (!payloadResult) {
            return _failExchange(item, payloadResult.error(),
                                 "exchange payload read failed");
        }
        return item.request.ack(item.handle, *payloadResult, sentAtUs,
                                ::platform::get_time_us());
    }

    ReturnCode _receiveData(const Header &header) {
        auto payloadResult = _receivePayloadIntoScratch(header);
        if (!payloadResult) {
            _resetConnection("data payload read failed");
            return OK();
        }

        auto *handler = _findHandler(header.payloadType);
        if (handler == nullptr || handler->onData == nullptr) {
            FAIL_IF_ERR_FWD(
                _transceiver.sendControl(FrameType::Nack, header.sequenceNumber,
                                         header.payloadType, nullptr,
                                         FrameTurn::Reaction),
                "Failed to send RS485 data nack for %s", Derived::name);
            return OK();
        }

        auto receivedAtUs = ::platform::get_time_us();
        auto payload =
            std::span<const std::byte>(_rxPayload.data(), *payloadResult);
        log_trace_packet("data.received", header, Derived::name);
        auto ret = handler->onData(handler->owner, header.payloadType, payload,
                                   receivedAtUs);
        auto reaction = ret.ok() ? FrameType::Ack : FrameType::Nack;
        FAIL_IF_ERR_FWD(_transceiver.sendControl(
                            reaction, header.sequenceNumber, header.payloadType,
                            nullptr, FrameTurn::Reaction),
                        "Failed to send RS485 data reaction for %s",
                        Derived::name);
        return OK();
    }

    ReturnCode _receiveRequest(const Header &header) {
        auto payloadResult = _receivePayloadIntoScratch(header);
        if (!payloadResult) {
            _resetConnection("request payload read failed");
            return OK();
        }

        auto *handler = _findHandler(header.payloadType);
        if (handler == nullptr || handler->onRequest == nullptr) {
            FAIL_IF_ERR_FWD(
                _transceiver.sendControl(FrameType::Nack, header.sequenceNumber,
                                         header.payloadType, nullptr,
                                         FrameTurn::Reaction),
                "Failed to send RS485 request nack for %s", Derived::name);
            return OK();
        }

        auto receivedAtUs = ::platform::get_time_us();
        auto request =
            std::span<const std::byte>(_rxPayload.data(), *payloadResult);
        log_trace_packet("request.received", header, Derived::name);
        auto responseResult =
            handler->onRequest(handler->owner, header.payloadType, request,
                               handler->response, receivedAtUs);
        if (!responseResult) {
            FAIL_IF_ERR_FWD(
                _transceiver.sendControl(FrameType::Nack, header.sequenceNumber,
                                         header.payloadType, nullptr,
                                         FrameTurn::Reaction),
                "Failed to send RS485 request handler nack for %s",
                Derived::name);
            return OK();
        }
        auto responseLength = *responseResult;
        auto responsePayload = handler->response.first(responseLength);

        return _transceiver.sendFrame(FrameType::Response, header.payloadType,
                                      responsePayload, header.sequenceNumber,
                                      nullptr, FrameTurn::Reaction);
    }

    static ReturnCode _beforeRequestWrite(void *owner, int64_t sentAtUs) {
        auto *ctx = static_cast<RequestWriteContext *>(owner);
        if (ctx == nullptr || ctx->request == nullptr ||
            ctx->request->onBeforeRequest == nullptr) {
            return OK();
        }
        return ctx->request->onBeforeRequest(
            ctx->request->owner, ctx->request->payloadType, sentAtUs);
    }

    std::expected<uint16_t, ReturnCode>
    _receivePayloadIntoScratch(const Header &header) {
        FAIL_IF(header.payloadLength > _rxPayload.size(),
                std::unexpected(ERR(CoreError, InvalidArgument)),
                "RS485 payload length %u exceeds scratch size %zu for %s",
                header.payloadLength, _rxPayload.size(), Derived::name);
        return _receiveSynchronousPayload(header, _rxPayload);
    }

    std::expected<Header, ReturnCode>
    _receiveSynchronousHeader(uint32_t timeoutMs) {
        metrics().addSyncHeaderRead();
        auto result = _transceiver.receiveHeader(timeoutMs);
        if (!result && result.error() == ERR(CoreError, Timeout)) {
            metrics().addSyncHeaderTimeout();
        }
        return result;
    }

    std::expected<uint16_t, ReturnCode>
    _receiveSynchronousPayload(const Header &header,
                               std::span<std::byte> buffer) {
        metrics().addSyncPayloadRead();
        return _transceiver.receivePayload(header, buffer);
    }

    FrameHandler *_findHandler(PayloadType payloadType) {
        for (auto &handler : _handlers) {
            if (handler.owner != nullptr &&
                handler.payloadType == payloadType) {
                return &handler;
            }
        }
        return nullptr;
    }

    ReturnCode _failWrite(const TxQueueItem &item, ReturnCode reason,
                          const char *resetReason) {
        if (_shouldResetAfterTransactionFailure(reason)) {
            _resetConnection(resetReason);
        }
        return item.request.nack(item.handle, reason);
    }

    ReturnCode _failExchange(const ExchangeQueueItem &item, ReturnCode reason,
                             const char *resetReason) {
        if (_shouldResetAfterTransactionFailure(reason)) {
            _resetConnection(resetReason);
        }
        return item.request.nack(item.handle, reason);
    }

    [[nodiscard]] static bool
    _shouldResetAfterTransactionFailure(ReturnCode reason) {
        return reason == ERR(CoreError, Timeout) ||
               reason == ERR(CoreError, InvalidState) ||
               reason == ERR(WireError, Corrupted) ||
               reason == ERR(WireError, CrcError) ||
               reason == ERR(WireError, SequenceError);
    }

    void _failPendingTransactions(ReturnCode reason) {
        if (_txQueue != nullptr) {
            TxQueueItem item{};
            while (Totem::Queue::Platform::receive(_txQueue, &item, 0).ok()) {
                (void)item.request.nack(item.handle, reason);
            }
        }
        if (_exchangeQueue != nullptr) {
            ExchangeQueueItem item{};
            while (Totem::Queue::Platform::receive(_exchangeQueue, &item, 0)
                       .ok()) {
                (void)item.request.nack(item.handle, reason);
            }
        }
        (void)_updateAttentionLine();
    }

    ReturnCode _updateAttentionLine() {
        if constexpr (Derived::sendsHeartbeat) {
            return OK();
        } else {
            if (!_attention.configured()) {
                return OK();
            }
            const bool hasPendingTx =
                _txQueue != nullptr &&
                Totem::Queue::Platform::size(_txQueue) > 0;
            const bool hasPendingExchange =
                _exchangeQueue != nullptr &&
                Totem::Queue::Platform::size(_exchangeQueue) > 0;
            const bool hasPending = hasPendingTx || hasPendingExchange;
            return _attention.setAsserted(hasPending);
        }
    }

    bool _consumeAttentionRequest() {
        if constexpr (!Derived::sendsHeartbeat) {
            return false;
        } else {
            const bool edgeRequested =
                _attentionRequested.exchange(false, std::memory_order_acq_rel);
            if (edgeRequested) {
                return true;
            }
            if (!_attention.configured()) {
                return false;
            }
            auto asserted = _attention.asserted();
            return asserted.has_value() && *asserted;
        }
    }

    void _finishTaskStep(int64_t startedAtUs) {
        const auto nowUs = ::platform::get_time_us();
        if (nowUs >= startedAtUs) {
            const auto elapsedUs = static_cast<uint32_t>(nowUs - startedAtUs);
            metrics().recordStepDuration(elapsedUs);
        }
    }

    Totem::TaskController::RunnerKey _task = 0;

    Totem::Queue::Platform::Storage<TxQueueItem, UartNodeConfig::txQueueSize>
        _txQueueStorage;
    Totem::Queue::Handle _txQueue = nullptr;

    Totem::Queue::Platform::Storage<ExchangeQueueItem,
                                    UartNodeConfig::txQueueSize>
        _exchangeQueueStorage;
    Totem::Queue::Handle _exchangeQueue = nullptr;

    std::array<FrameHandler, 4> _handlers{};
    std::array<std::byte, 256> _rxPayload{};

    WriteRequestHandle _nextWriteHandle = 1;
    ExchangeRequestHandle _nextExchangeHandle = 1;

    uint32_t _lastHeartbeatSentMs = 0;
    uint32_t _lastHeartbeatReceivedMs = 0;
    uint8_t _heartbeatSequence = 0;
    uint8_t _missedHeartbeats = 0;
    bool _heartbeatAwaitingResponse = false;
    uint32_t _lastNopSentMs = 0;
    AttentionLine _attention;
    std::atomic<bool> _attentionRequested{false};

    Transceiver<Mode> _transceiver;
};

template <class Derived> struct NodeContract {
    static_assert(IsNamedEntity<Derived>,
                  "Derived type must be a named entity");
};

} // namespace Totem::Wire::Rs485::detail
