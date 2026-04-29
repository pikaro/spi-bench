#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "Macros/Facade.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "Types/Error.hpp"
#include "Types/Signal.hpp"
#include "Wire/Spi/Interfaces/MasterConfig.hpp"
#include "Wire/Spi/detail/Metrics.hpp"
#include "Wire/Spi/detail/PlatformSelect.hpp"
#include "Wire/Spi/detail/Transceiver.hpp"
#include "Wire/Spi/detail/Types.hpp"
#include "Wire/detail/AttentionLine.hpp"
#include <array>
#include <atomic>
#include <cstddef>

namespace Totem::Wire::Spi::detail {

class Master : public HasLifecycle<Master, MasterConfig>,
               public HasTaskController<Master, MasterConfig> {
    friend class HasLifecycle<Master, MasterConfig>;
    friend struct LifecycleContract<Master, MasterConfig>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<Master, MasterConfig>;
    friend struct TaskControllerContract<Master>;
    friend struct TaskController::TaskHooks::Contract<Master>;

  public:
    DELETE_COPY(Master)
    DELETE_MOVE(Master)

    static constexpr const char *name = "Spi::Master";
    static constexpr LogComponent logComponent =
        Totem::Wire::Spi::detail::logComponent;

    explicit Master(TaskController::IRegistry &registry)
        : HasTaskController<Master, MasterConfig>(registry) {}

    [[nodiscard]] DeviceHandle defaultDevice() const { return _device; }
    [[nodiscard]] bool ready() const { return _transceiver.ready(); }

    ReturnCode registerHandler(const FrameHandler &handler) {
        return _transceiver.registerHandler(handler);
    }

  private:
    ReturnCode _onBegin() {
        (void)metrics();

        _log_i("SPI master initializing bus");
        FAIL_IF_ERR(_bus.init(config().bus), ERR(CoreError, OperationFailed),
                    "Failed to initialize SPI master bus");
        _log_i("SPI master bus initialized");

        _log_i("SPI master adding device");
        auto deviceResult = _bus.addDevice(config().device);
        if (!deviceResult) {
            (void)_bus.deinit();
            FAIL(deviceResult.error(), "Failed to add SPI master device");
        }
        _log_i("SPI master device added");
        _device = *deviceResult;
        _transceiver.reset();
        _transceiver.setAutoHelloResponse(false);
        FAIL_IF_ERR_FWD(_transceiver.queueHello(),
                        "Failed to prepare SPI master hello slot");
        _lastHandshakeAttemptMs = ::platform::get_time();

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

        FAIL_IF_ERR_FWD(_attention.initInput(this->config().attentionPin, this,
                                             _onAttentionLine),
                        "Failed to initialize SPI master attention input");
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        ret.combine(_attention.deinit());
        _task = 0;
        ret.combine(this->_endTaskController());
        _device = DeviceHandle::invalid();
        ret.combine(_bus.deinit());
        return ret;
    }

    ReturnCode _onTaskStep() {
        metrics().addTaskStep();
        const bool attentionRequested = _consumeAttentionRequest();
        return _runTurn(attentionRequested);
    }

    ReturnCode _onTaskNotify(Signal signal) {
        if (signal == Signal::SpiAttention) {
            metrics().addAttentionWake();
        }
        return OK();
    }

    static void
    _onAttentionLine(void *owner,
                     Totem::Wire::detail::AttentionLineEvent event) {
        auto *self = static_cast<Master *>(owner);
        if (self == nullptr ||
            event != Totem::Wire::detail::AttentionLineEvent::Asserted) {
            return;
        }
        self->_attentionRequested.store(true, std::memory_order_release);
        self->_wakeFromIsr(Signal::SpiAttention);
    }

    void _wakeFromIsr(Signal signal = Signal::Ping) {
        if (_task == 0) {
            return;
        }
        this->_taskController.signalTaskFromIsr(_task, signal);
    }

    bool _consumeAttentionRequest() {
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

    ReturnCode _runTurn(bool attentionRequested) {
        if (!_device.valid()) {
            return OK();
        }

        if (!attentionRequested && _transceiver.ready() &&
            !_transceiver.hasPendingTx()) {
            return OK();
        }

        if (!_transceiver.ready()) {
            const auto nowMs = ::platform::get_time();
            if (static_cast<uint32_t>(nowMs - _lastHandshakeAttemptMs) >=
                handshakeRetryMs) {
                _log_i("SPI master hello retry turn=%lu state=%u pending=%u",
                       static_cast<unsigned long>(_turnCount),
                       static_cast<unsigned>(_transceiver.state()),
                       static_cast<unsigned>(_transceiver.hasPendingTx()));
                FAIL_IF_ERR_FWD(_transceiver.queueHello(),
                                "Failed to retry SPI hello");
                _lastHandshakeAttemptMs = nowMs;
            }
        }

        const auto tx = _transceiver.finalizeTx();
        FAIL_IF(tx.size() > _rxBuffer.size(), ERR(CoreError, Overflow),
                "SPI master RX buffer too small");
        auto rx = std::span<std::byte>(_rxBuffer.data(), tx.size());
        const auto startedAtUs = ::platform::get_time_us();
        _turnCount++;
        if (!_transceiver.ready() && _turnCount <= 5) {
            _log_d("SPI master debug hello turn=%lu txLen=%u txPtr=%p rxPtr=%p",
                   static_cast<unsigned long>(_turnCount),
                   static_cast<unsigned>(tx.size()), _transceiver.txData(),
                   _rxBuffer.data());
        }
        auto ret = _bus.transfer(_device, Transfer{
                                              .txBuffer = tx,
                                              .rxBuffer = rx,
                                              .timeoutMs = 10,
                                          });
        if (!ret.ok()) {
            if (!_transceiver.ready()) {
                _log_w("SPI master hello transfer failed turn=%lu: " ERR_FMT,
                       static_cast<unsigned long>(_turnCount), ERR_ARG(ret));
            }
            return ret == ERR(CoreError, Timeout) ? OK() : ret;
        }

        const auto receivedAtUs = ::platform::get_time_us();
        metrics().addTurn();
        metrics().recordTurnDuration(
            static_cast<uint32_t>(receivedAtUs - startedAtUs));
        const bool wasReady = _transceiver.ready();
        auto parseRet = _transceiver.parseRx(rx, receivedAtUs);
        if (!parseRet.ok()) {
            if (!_transceiver.ready() &&
                (parseRet == ERR(WireError, Corrupted) ||
                 parseRet == ERR(WireError, CrcError) ||
                 parseRet == ERR(WireError, SequenceError))) {
                _log_w("SPI master hello RX invalid turn=%lu first=%02x "
                       "second=%02x len=%u: " ERR_FMT,
                       static_cast<unsigned long>(_turnCount),
                       std::to_integer<unsigned>(rx[0]),
                       std::to_integer<unsigned>(rx[1]),
                       static_cast<unsigned>(rx.size()), ERR_ARG(parseRet));
                // if (std::to_integer<unsigned>(rx[0]) == SlotHeader::preamble
                // &&
                //     rx.size() >= SlotHeader::size) {
                _log_w("SPI master invalid header bytes: %02x %02x %02x "
                       "%02x %02x %02x %02x %02x %02x %02x %02x %02x "
                       "%02x %02x %02x %02x %02x %02x %02x",
                       std::to_integer<unsigned>(rx[0]),
                       std::to_integer<unsigned>(rx[1]),
                       std::to_integer<unsigned>(rx[2]),
                       std::to_integer<unsigned>(rx[3]),
                       std::to_integer<unsigned>(rx[4]),
                       std::to_integer<unsigned>(rx[5]),
                       std::to_integer<unsigned>(rx[6]),
                       std::to_integer<unsigned>(rx[7]),
                       std::to_integer<unsigned>(rx[8]),
                       std::to_integer<unsigned>(rx[9]),
                       std::to_integer<unsigned>(rx[10]),
                       std::to_integer<unsigned>(rx[11]),
                       std::to_integer<unsigned>(rx[12]),
                       std::to_integer<unsigned>(rx[13]),
                       std::to_integer<unsigned>(rx[14]),
                       std::to_integer<unsigned>(rx[15]),
                       std::to_integer<unsigned>(rx[16]),
                       std::to_integer<unsigned>(rx[17]),
                       std::to_integer<unsigned>(rx[18]));
                // }
                _transceiver.advanceTxSlot();
                FAIL_IF_ERR_FWD(_transceiver.queueHello(),
                                "Failed to queue SPI hello after invalid RX");
                return OK();
            }
            return parseRet;
        }
        if (!_transceiver.ready()) {
            _log_i("SPI master hello turn accepted turn=%lu state=%u",
                   static_cast<unsigned long>(_turnCount),
                   static_cast<unsigned>(_transceiver.state()));
        }
        if (!wasReady && _transceiver.ready()) {
            _log_i("SPI master handshake complete");
        }
        if (!_transceiver.ready()) {
            FAIL_IF_ERR_FWD(_transceiver.queueHello(),
                            "Failed to queue next SPI hello");
        }
        return OK();
    }

    static constexpr uint32_t handshakeRetryMs = 500;

    Platform::SpiMasterBus _bus{};
    Transceiver<4096> _transceiver{};
    DeviceHandle _device{};
    alignas(4) std::array<std::byte, 4096> _rxBuffer{};
    Totem::Wire::detail::AttentionLine _attention{};
    Totem::TaskController::RunnerKey _task = 0;
    std::atomic<bool> _attentionRequested{false};
    uint32_t _lastHandshakeAttemptMs = 0;
    uint32_t _turnCount = 0;
};

inline constexpr LifecycleContract<Master, MasterConfig> _master_lifecycle;
inline constexpr TaskControllerContract<Master> _master_task_controller;
inline constexpr TaskController::TaskHooks::Contract<Master> _master_task_hook;

} // namespace Totem::Wire::Spi::detail
