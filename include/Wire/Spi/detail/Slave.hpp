#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "Macros/Facade.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "Types/Error.hpp"
#include "Types/Signal.hpp"
#include "Wire/Interfaces/Request.hpp"
#include "Wire/Spi/detail/Metrics.hpp"
#include "Wire/Spi/detail/PlatformSelect.hpp"
#include "Wire/Spi/detail/Transceiver.hpp"
#include "Wire/Spi/detail/Types.hpp"
#include "Wire/detail/AttentionLine.hpp"
#include <atomic>
#include <array>
#include <cstddef>
#include <cstring>
#include <optional>

namespace Totem::Wire::Spi::detail {

class Slave : public HasLifecycle<Slave, SlaveConfig>,
              public HasTaskController<Slave, SlaveConfig> {
    friend class HasLifecycle<Slave, SlaveConfig>;
    friend struct LifecycleContract<Slave, SlaveConfig>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<Slave, SlaveConfig>;
    friend struct TaskControllerContract<Slave>;
    friend struct TaskController::TaskHooks::Contract<Slave>;

  public:
    DELETE_COPY(Slave)
    DELETE_MOVE(Slave)

    static constexpr const char *name = "Spi::Slave";
    static constexpr LogComponent logComponent =
        Totem::Wire::Spi::detail::logComponent;

    explicit Slave(TaskController::IRegistry &registry)
        : HasTaskController<Slave, SlaveConfig>(registry) {}

    ReturnCode registerHandler(const FrameHandler &handler) {
        return _transceiver.registerHandler(handler);
    }

    [[nodiscard]] bool ready() const { return _transceiver.ready(); }

    ReturnCode exchange(const Totem::Wire::ExchangeRequest &request) {
        FAIL_IF(!request.validate(), ERR(CoreError, InvalidArgument),
                "Invalid SPI exchange request");
        FAIL_IF(_pendingExchange.has_value() || _exchangeInFlight,
                ERR(CoreError, InvalidState), "SPI exchange already pending");

        _pendingExchange = request;
        _wake(Signal::Ping);
        return OK();
    }

  private:
    ReturnCode _onBegin() {
        (void)metrics();

        _log_i("SPI slave initializing device");
        FAIL_IF_ERR_FWD(_device.init(config()), "Failed to initialize SPI slave");
        _log_i("SPI slave device initialized");
        _device.registerCompletionCallback(this, _onTransferComplete);
        _transceiver.reset();
        _transceiver.setAutoHelloResponse(true);
        _transceiver.registerResponseCallback(this, _onResponseFrame);
        FAIL_IF_ERR_FWD(_attention.initOutput(this->config().attentionPin),
                        "Failed to initialize SPI slave attention output");
        FAIL_IF_ERR_FWD(_updateAttentionLine(),
                        "Failed to update SPI slave attention output");
        FAIL_IF_ERR_FWD(_queueTransfer(),
                        "Failed to queue initial SPI slave transfer");

        auto taskHooks = TaskController::TaskHooks::bind(*this);
        FAIL_IF_ERR_FWD(this->_beginTaskController(this->config().task),
                        "Failed to begin task controller for %s", name);
        auto taskAddResult =
            this->_taskController.addTask(this->config().task.name, taskHooks);
        FAIL_IF_UNEXPECTED(task, taskAddResult, taskAddResult.error(),
                           "Failed to bind task hooks for %s", name);
        _task = task;
        FAIL_IF_ERR_FWD(
            this->_taskController.startTask(_task, this->config().task),
            "Failed to start task for %s", name);
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        _task = 0;
        ret.combine(_failExchange(ERR(CoreError, InvalidState)));
        ret.combine(_attention.deinit());
        ret.combine(this->_endTaskController());
        ret.combine(_device.deinit());
        return ret;
    }

    ReturnCode _onTaskStep() {
        metrics().addTaskStep();
        FAIL_IF_ERR_FWD(_completeTransfer(), "Failed to complete SPI transfer");
        if (!_transferQueued) {
            FAIL_IF_ERR_FWD(_preparePendingExchange(),
                            "Failed to prepare pending SPI exchange");
            FAIL_IF_ERR_FWD(_queueTransfer(),
                            "Failed to queue SPI slave transfer");
            FAIL_IF_ERR_FWD(_updateAttentionLine(),
                            "Failed to update SPI slave attention output");
        }
        return OK();
    }

    ReturnCode _preparePendingExchange() {
        if (!_pendingExchange.has_value()) {
            return OK();
        }
        auto request = *_pendingExchange;
        auto ret = _transceiver.queueRequest(request.payloadType,
                                             request.request);
        if (!ret.ok()) {
            auto callbackRet = request.nack(exchangeHandle, ret);
            _pendingExchange.reset();
            return callbackRet;
        }
        _activeExchange = request;
        _pendingExchange.reset();
        _exchangeInFlight = true;
        _exchangeSentAtUs = 0;
        return OK();
    }

    ReturnCode _onTaskNotify(Signal) { return OK(); }

    ReturnCode _updateAttentionLine() {
        const bool asserted = _transceiver.hasPendingTx();
        if (asserted != _attentionAsserted) {
            if (asserted) {
                metrics().addAttentionAssert();
            } else {
                metrics().addAttentionRelease();
            }
            _attentionAsserted = asserted;
        }
        return _attention.setAsserted(asserted);
    }

    ReturnCode _queueTransfer() {
        if (_transferQueued) {
            return OK();
        }
        const auto tx = _transceiver.finalizeTx();
        FAIL_IF(tx.size() > _rxBuffer.size(), ERR(CoreError, Overflow),
                "SPI slave RX buffer too small");
        auto rx = std::span<std::byte>(_rxBuffer.data(), tx.size());
        if (!_transceiver.ready() && _queueCount < 5) {
            _log_d("SPI slave debug queue transfer=%lu txLen=%u txPtr=%p "
                   "rxPtr=%p",
                   static_cast<unsigned long>(_queueCount + 1),
                   static_cast<unsigned>(tx.size()), _transceiver.txData(),
                   _rxBuffer.data());
        }
        if (_exchangeInFlight && _exchangeSentAtUs == 0) {
            _exchangeSentAtUs = ::platform::get_time_us();
        }
        FAIL_IF_ERR_FWD(_device.queueTransfer(Transfer{
                            .txBuffer = tx,
                            .rxBuffer = rx,
                            .timeoutMs = 0,
                        }),
                        "Failed to queue SPI slave DMA transfer");
        _queuedRxSize = rx.size();
        _transferQueued = true;
        _queueCount++;
        return OK();
    }

    static ReturnCode _onResponseFrame(void *owner, const FrameView &frame,
                                       int64_t receivedAtUs) {
        auto *self = static_cast<Slave *>(owner);
        FAIL_IF(self == nullptr, ERR(CoreError, InvalidArgument),
                "Invalid SPI response owner");
        return self->_handleResponseFrame(frame, receivedAtUs);
    }

    ReturnCode _handleResponseFrame(const FrameView &frame,
                                    int64_t receivedAtUs) {
        if (!_exchangeInFlight ||
            frame.header.payloadType != _activeExchange.payloadType) {
            return OK();
        }
        if (frame.payload.size() > _activeExchange.response.size()) {
            auto ret = _activeExchange.nack(exchangeHandle,
                                           ERR(CoreError, Overflow));
            _clearActiveExchange();
            return ret;
        }
        std::memcpy(_activeExchange.response.data(), frame.payload.data(),
                    frame.payload.size());
        auto ret = _activeExchange.ack(
            exchangeHandle, static_cast<uint16_t>(frame.payload.size()),
            _exchangeSentAtUs, receivedAtUs);
        _clearActiveExchange();
        return ret;
    }

    void _clearActiveExchange() {
        _activeExchange = {};
        _exchangeInFlight = false;
        _exchangeSentAtUs = 0;
    }

    ReturnCode _failExchange(ReturnCode error) {
        auto ret = OK();
        if (_pendingExchange.has_value()) {
            ret.combine(_pendingExchange->nack(exchangeHandle, error));
            _pendingExchange.reset();
        }
        if (_exchangeInFlight) {
            ret.combine(_activeExchange.nack(exchangeHandle, error));
            _clearActiveExchange();
        }
        return ret;
    }

    ReturnCode _completeTransfer() {
        if (!_transferQueued) {
            return OK();
        }
        auto result = _device.waitTransfer(0);
        if (!result) {
            if (!_transceiver.ready() && result.error() == ERR(CoreError, Timeout)) {
                _waitTimeouts++;
                if (_waitTimeouts <= 5) {
                    _log_d("SPI slave debug wait timeout=%lu transferQueued=%u",
                           static_cast<unsigned long>(_waitTimeouts),
                           static_cast<unsigned>(_transferQueued));
                }
                if ((_waitTimeouts % 50) == 0) {
                    _log_i("SPI slave waiting for master clock timeouts=%lu "
                           "isrCompletions=%lu",
                           static_cast<unsigned long>(_waitTimeouts),
                           static_cast<unsigned long>(_completionCount.load(
                               std::memory_order_acquire)));
                }
            }
            return result.error() == ERR(CoreError, Timeout) ? OK()
                                                             : result.error();
        }
        _transferQueued = false;
        metrics().addTurn();
        _completedTransfers++;
        const auto bytes = result->bytesTransferred > 0
                               ? result->bytesTransferred
                               : _queuedRxSize;
        if (!_transceiver.ready()) {
            _log_i("SPI slave transfer complete count=%lu bytes=%u first=%02x "
                   "second=%02x isrCompletions=%lu",
                   static_cast<unsigned long>(_completedTransfers),
                   static_cast<unsigned>(bytes),
                   bytes > 0 ? std::to_integer<unsigned>(_rxBuffer[0]) : 0,
                   bytes > 1 ? std::to_integer<unsigned>(_rxBuffer[1]) : 0,
                   static_cast<unsigned long>(_completionCount.load(
                       std::memory_order_acquire)));
        }
        FAIL_IF(bytes > _rxBuffer.size(), ERR(CoreError, Overflow),
                "SPI slave transfer exceeded RX buffer");
        if (bytes == 0) {
            return OK();
        }
        if (bytes < SlotHeader::size) {
            _log_w("SPI slave short transfer before handshake bytes=%u",
                   static_cast<unsigned>(bytes));
            metrics().addReset();
            _transceiver.reset();
            _transceiver.setAutoHelloResponse(true);
            FAIL_IF_ERR_FWD(_failExchange(ERR(WireError, Corrupted)),
                            "Failed to fail SPI exchange after short input");
            return OK();
        }
        auto rx = std::span<const std::byte>(_rxBuffer.data(), bytes);
        const bool wasReady = _transceiver.ready();
        auto ret = _transceiver.parseRx(rx, ::platform::get_time_us());
        if (!ret.ok()) {
            if (ret == ERR(WireError, Corrupted) ||
                ret == ERR(WireError, CrcError) ||
                ret == ERR(WireError, SequenceError)) {
                _log_w("SPI slave RX invalid before handshake bytes=%u "
                       "first=%02x second=%02x: " ERR_FMT,
                       static_cast<unsigned>(bytes),
                       std::to_integer<unsigned>(_rxBuffer[0]),
                       std::to_integer<unsigned>(_rxBuffer[1]), ERR_ARG(ret));
                metrics().addReset();
                _transceiver.reset();
                _transceiver.setAutoHelloResponse(true);
                FAIL_IF_ERR_FWD(_failExchange(ret),
                                "Failed to fail SPI exchange after reset");
                return OK();
            }
            return ret;
        }
        if (!wasReady && _transceiver.ready()) {
            _log_i("SPI slave handshake complete");
        }
        return OK();
    }

    static void _onTransferComplete(void *owner) {
        auto *self = static_cast<Slave *>(owner);
        if (self == nullptr) {
            return;
        }
        self->_completionCount.fetch_add(1, std::memory_order_release);
        self->_wakeFromIsr(Signal::SpiTransfer);
    }

    void _wakeFromIsr(Signal signal = Signal::Ping) {
        if (_task == 0) {
            return;
        }
        this->_taskController.signalTaskFromIsr(_task, signal);
    }

    void _wake(Signal signal = Signal::Ping) {
        if (_task == 0) {
            return;
        }
        (void)this->_taskController.signalTask(_task, signal);
    }

    static constexpr Totem::Wire::ExchangeRequestHandle exchangeHandle = 1;

    Platform::SpiSlaveDevice _device{};
    Transceiver<4096> _transceiver{};
    alignas(4) std::array<std::byte, 4096> _rxBuffer{};
    Totem::Wire::detail::AttentionLine _attention{};
    Totem::TaskController::RunnerKey _task = 0;
    size_t _queuedRxSize = 0;
    bool _transferQueued = false;
    bool _attentionAsserted = false;
    uint32_t _waitTimeouts = 0;
    uint32_t _completedTransfers = 0;
    uint32_t _queueCount = 0;
    std::atomic<uint32_t> _completionCount{0};
    std::optional<Totem::Wire::ExchangeRequest> _pendingExchange =
        std::nullopt;
    Totem::Wire::ExchangeRequest _activeExchange{};
    bool _exchangeInFlight = false;
    int64_t _exchangeSentAtUs = 0;
};

inline constexpr LifecycleContract<Slave, SlaveConfig> _slave_lifecycle;
inline constexpr TaskControllerContract<Slave> _slave_task_controller;
inline constexpr TaskController::TaskHooks::Contract<Slave> _slave_task_hook;

} // namespace Totem::Wire::Spi::detail
