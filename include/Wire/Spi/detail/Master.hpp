#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "Macros/Facade.hpp"
#include "Queue/Facade.hpp"
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
#include <optional>

namespace Totem::Wire::Spi::detail {

class Master : public HasLifecycle<Master, MasterConfig>,
               public HasTaskController<Master, MasterConfig> {
    friend class HasLifecycle<Master, MasterConfig>;
    friend struct LifecycleContract<Master, MasterConfig>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<Master, MasterConfig>;
    friend struct TaskControllerContract<Master>;
    friend struct TaskController::TaskHooks::Contract<Master>;

    struct TxQueueItem {
        Totem::Wire::WriteRequestHandle handle = 0;
        Totem::Wire::WriteRequest request{};
    };

    struct PendingWrite {
        uint16_t sequence = 0;
        Totem::Wire::WriteRequestHandle handle = 0;
        Totem::Wire::WriteRequest request{};
        size_t length = 0;
        uint32_t sentAtMs = 0;
        int64_t sentAtUs = 0;
        bool occupied = false;
    };

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

    ReturnCode send(const Totem::Wire::WriteRequest &request) {
        FAIL_IF(!request.validate(), ERR(CoreError, InvalidArgument),
                "Invalid SPI write request");
        FAIL_IF_NOT(ready(), ERR(CoreError, InvalidState),
                    "Cannot send SPI data before master link is ready");
        FAIL_IF_NULL(_txQueue, ERR(CoreError, InvalidState),
                     "SPI master TX queue is not initialized");
        auto item = TxQueueItem{
            .handle = _nextWriteHandle++,
            .request = request,
        };
        FAIL_IF_ERR_FWD(Totem::Queue::Platform::send(_txQueue, &item, 0),
                        "Failed to enqueue SPI master write request");
        _recordQueuedWriteTime();
        return _wake(Signal::Ping);
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
        _transceiver.setPreserveTxSlotSequenceOnReset(true);
        _transceiver.reset();
        _transceiver.setAutoHelloResponse(false);
        _transceiver.registerAckCallback(this, _onAckFrame);
        FAIL_IF_ERR_FWD(_transceiver.queueHello(),
                        "Failed to prepare SPI master hello slot");
        _lastHandshakeAttemptMs = ::platform::get_time();
        auto txQueueResult = Totem::Queue::Platform::create(_txQueueStorage);
        FAIL_IF_UNEXPECTED_FWD(txQueue, txQueueResult,
                               "Failed to create SPI master TX queue");
        _txQueue = txQueue;

        DEFAULT_TASK();
        _task = task;
        START_TASK();

        FAIL_IF_ERR_FWD(_attention.initInput(this->config().attentionPin, this,
                                             _onAttentionLine),
                        "Failed to initialize SPI master attention input");
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        ret.combine(_attention.deinit());
        _task = 0;
        ret.combine(_failQueuedWrites(ERR(CoreError, InvalidState)));
        ret.combine(_failPendingWrites(ERR(CoreError, InvalidState)));
        if (_txQueue != nullptr) {
            ret.combine(Totem::Queue::Platform::destroy(_txQueue));
            _txQueue = nullptr;
        }
        _queuedWriteSinceUs.store(0, std::memory_order_relaxed);
        ret.combine(this->_endTaskController());
        _device = DeviceHandle::invalid();
        ret.combine(_bus.deinit());
        return ret;
    }

    ReturnCode _onTaskStep() {
        metrics().addTaskStep();
        if (this->config().maxTurnsPerStep <= 1 ||
            this->config().serviceBudgetMs == 0) {
            return _runTurn(_consumeAttentionRequest());
        }

        const auto startedAtMs = ::platform::get_time();
        auto ret = _runTurn(_consumeAttentionRequest());
        for (uint8_t turn = 1;
             ret.ok() && turn < this->config().maxTurnsPerStep; turn++) {
            if (!_transceiver.ready()) {
                break;
            }
            if (static_cast<uint32_t>(::platform::get_time() - startedAtMs) >=
                this->config().serviceBudgetMs) {
                break;
            }
            _delayInterTurn();
            ret.combine(_runTurn(_consumeAttentionRequest()));
        }
        return ret;
    }

    ReturnCode _onTaskNotify(Signal signal) {
        if (signal == Signal::SpiAttention) {
            metrics().addAttentionWake();
        }
        return OK();
    }

    static void _onAttentionLine(void *owner,
                                 Totem::Wire::detail::AttentionLineEvent event,
                                 int64_t timestampUs) {
        auto *self = static_cast<Master *>(owner);
        if (self == nullptr ||
            event != Totem::Wire::detail::AttentionLineEvent::Asserted) {
            return;
        }
        self->_transceiver.recordAttentionAsserted(
            timestampUs == 0 ? ::platform::get_time_us() : timestampUs);
        self->_attentionRequested.store(true, std::memory_order_release);
        self->_wakeFromIsr(Signal::SpiAttention);
    }

    void _wakeFromIsr(Signal signal = Signal::Ping) {
        if (_task == 0) {
            return;
        }
        this->_taskController.signalTaskFromIsr(_task, signal);
    }

    ReturnCode _wake(Signal signal = Signal::Ping) {
        if (_task == 0) {
            return OK();
        }
        return this->_taskController.signalTaskDirect(_task, signal);
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

        const auto nowMs = ::platform::get_time();
        if (attentionRequested) {
            _noSlotBackoffUntilUs = 0;
        } else {
            _delayNoSlotBackoffIfNeeded();
        }
        if (_transceiver.ready()) {
            FAIL_IF_ERR_FWD(_expirePendingWrites(nowMs),
                            "Failed to expire SPI master pending writes");
        }

        bool heartbeatQueued = false;
        if (_heartbeatDue(nowMs) && !_transceiver.hasPendingTx() &&
            !_hasQueuedWrites() && !_hasPendingWrites()) {
            FAIL_IF_ERR_FWD(_transceiver.queueHeartbeat(),
                            "Failed to queue SPI master heartbeat");
            heartbeatQueued = true;
            _nextHeartbeatAtMs = nowMs + heartbeatIntervalMs;
        }

        const bool pollPendingWrites =
            !_attention.configured() && _hasPendingWrites();
        const bool canQueueWrite =
            _hasQueuedWrites() && _findFreePendingWrite() != nullptr;
        if (!attentionRequested && _transceiver.ready() &&
            !_transceiver.hasPendingTx() && !canQueueWrite &&
            !pollPendingWrites && !heartbeatQueued) {
            return OK();
        }

        if (!_transceiver.ready()) {
            if (static_cast<uint32_t>(nowMs - _lastHandshakeAttemptMs) >=
                handshakeRetryMs) {
                _log_v("SPI master hello retry turn=%lu state=%u pending=%u",
                       static_cast<unsigned long>(_turnCount),
                       static_cast<unsigned>(_transceiver.state()),
                       static_cast<unsigned>(_transceiver.hasPendingTx()));
                FAIL_IF_ERR_FWD(_transceiver.queueHello(),
                                "Failed to retry SPI hello");
                _lastHandshakeAttemptMs = nowMs;
            }
        }

        if (_transceiver.ready()) {
            if (_shouldCoalesceLocalWrite(attentionRequested)) {
                _delayLocalWriteCoalesce();
                attentionRequested =
                    attentionRequested || _consumeAttentionRequest();
            }
            FAIL_IF_ERR_FWD(_processQueuedWrites(),
                            "Failed to process SPI master write queue");
            _refreshQueuedWriteTime();
            if (_shouldWidenReceiveWindow(attentionRequested)) {
                FAIL_IF_ERR_FWD(_transceiver.ensureTxWindow(
                                    this->config().attentionReceiveWindowBytes),
                                "Failed to widen SPI master active RX "
                                "window");
            }
        }

        const auto startedAtUs = ::platform::get_time_us();
        const auto tx = _transceiver.finalizeTx();
        FAIL_IF(tx.size() > _rxBuffer.size(), ERR(CoreError, Overflow),
                "SPI master RX buffer too small");
        auto rx = std::span<std::byte>(_rxBuffer.data(), tx.size());
        _turnCount++;
        if (!_transceiver.ready() && _turnCount <= 5) {
            _log_v("SPI master debug hello turn=%lu txLen=%u txPtr=%p rxPtr=%p",
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
        const auto previousRxSequence = _transceiver.lastReceivedSequence();
        const bool peerHadSlot = _transceiver.hasProtocolPreamble(rx);
        if (peerHadSlot) {
            _transceiver.markTxConsumed();
        }
        auto parseRet = _transceiver.parseRx(rx, receivedAtUs);
        const bool helloResynced = _transceiver.consumeHelloResynced();
        if (!parseRet.ok()) {
            if (!_transceiver.ready() &&
                (parseRet == ERR(WireError, Corrupted) ||
                 parseRet == ERR(WireError, CrcError) ||
                 parseRet == ERR(WireError, SequenceError))) {
                _log_v("SPI master hello RX invalid turn=%lu first=%02x "
                       "second=%02x len=%u: " ERR_FMT,
                       static_cast<unsigned long>(_turnCount),
                       std::to_integer<unsigned>(rx[0]),
                       std::to_integer<unsigned>(rx[1]),
                       static_cast<unsigned>(rx.size()), ERR_ARG(parseRet));
                if (rx.size() >= SlotHeader::size) {
                    _log_v("SPI master invalid header bytes: %02x %02x %02x "
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
                }
                _transceiver.advanceTxSlot();
                FAIL_IF_ERR_FWD(_transceiver.queueHello(),
                                "Failed to queue SPI hello after invalid RX");
                return OK();
            }
            if (parseRet == ERR(WireError, Corrupted) ||
                parseRet == ERR(WireError, CrcError) ||
                parseRet == ERR(WireError, SequenceError)) {
                _log_v("Dropping invalid SPI master RX slot: " ERR_FMT,
                       ERR_ARG(parseRet));
                _transceiver.advanceTxSlot();
                return OK();
            }
            return parseRet;
        }
        if (!peerHadSlot) {
            if (this->config().noSlotBackoffUs > 0) {
                _noSlotBackoffUntilUs =
                    receivedAtUs +
                    static_cast<int64_t>(this->config().noSlotBackoffUs);
            }
            return OK();
        }
        _noSlotBackoffUntilUs = 0;
        if (wasReady &&
            _transceiver.lastReceivedSequence() != previousRxSequence) {
            _recordPeerProgress(nowMs);
        } else if (heartbeatQueued) {
            FAIL_IF_ERR_FWD(_recordMissedHeartbeat(),
                            "Failed to handle missed SPI heartbeat");
            if (!_transceiver.ready()) {
                return OK();
            }
        }
        if (helloResynced) {
            metrics().addLinkRecovery(LinkRecoveryReason::HelloResync);
            _log_w("SPI master link recovery reason=%s; dropping stale "
                   "pending operations",
                   link_recovery_reason_name(
                       LinkRecoveryReason::HelloResync));
            FAIL_IF_ERR_FWD(_failQueuedWrites(ERR(WireError, SequenceError)),
                            "Failed to fail stale SPI master queued writes");
            FAIL_IF_ERR_FWD(_failPendingWrites(ERR(WireError, SequenceError)),
                            "Failed to fail stale SPI master pending writes");
        }
        if (!_transceiver.ready()) {
            _log_i("SPI master hello turn accepted turn=%lu state=%u",
                   static_cast<unsigned long>(_turnCount),
                   static_cast<unsigned>(_transceiver.state()));
        }
        if (!wasReady && _transceiver.ready()) {
            _log_i("SPI master handshake complete");
            _recordPeerProgress(nowMs);
        }
        if (!_transceiver.ready()) {
            FAIL_IF_ERR_FWD(_transceiver.queueHello(),
                            "Failed to queue next SPI hello");
        }
        return OK();
    }

    static ReturnCode _onAckFrame(void *owner, uint16_t sequence,
                                  ReturnCode result, int64_t receivedAtUs) {
        auto *self = static_cast<Master *>(owner);
        FAIL_IF(self == nullptr, ERR(CoreError, InvalidArgument),
                "Invalid SPI ack owner");
        return self->_completePendingWrite(sequence, result, receivedAtUs);
    }

    [[nodiscard]] bool _hasQueuedWrites() const {
        return _deferredWrite.has_value() ||
               (_txQueue != nullptr &&
                Totem::Queue::Platform::size(_txQueue) > 0);
    }

    [[nodiscard]] bool _hasPendingWrites() const {
        for (const auto &pending : _pendingWrites) {
            if (pending.occupied) {
                return true;
            }
        }
        return false;
    }

    void _recordQueuedWriteTime() {
        int64_t expected = 0;
        (void)_queuedWriteSinceUs.compare_exchange_strong(
            expected, ::platform::get_time_us(), std::memory_order_relaxed,
            std::memory_order_relaxed);
    }

    void _refreshQueuedWriteTime() {
        if (!_hasQueuedWrites()) {
            _queuedWriteSinceUs.store(0, std::memory_order_relaxed);
            return;
        }
        if (_queuedWriteSinceUs.load(std::memory_order_relaxed) == 0) {
            _recordQueuedWriteTime();
        }
    }

    [[nodiscard]] bool
    _shouldCoalesceLocalWrite(bool attentionRequested) const {
        if (this->config().localWriteCoalesceUs == 0 ||
            !_attention.configured() || attentionRequested ||
            !_transceiver.ready() || _transceiver.hasPendingTx() ||
            !_hasQueuedWrites()) {
            return false;
        }
        const auto queuedSinceUs =
            _queuedWriteSinceUs.load(std::memory_order_relaxed);
        if (queuedSinceUs <= 0) {
            return false;
        }
        const auto elapsedUs = ::platform::get_time_us() - queuedSinceUs;
        return elapsedUs >= 0 &&
               elapsedUs <
                   static_cast<int64_t>(this->config().localWriteCoalesceUs);
    }

    void _delayLocalWriteCoalesce() const {
        const auto queuedSinceUs =
            _queuedWriteSinceUs.load(std::memory_order_relaxed);
        const auto elapsedUs = ::platform::get_time_us() - queuedSinceUs;
        const auto windowUs =
            static_cast<int64_t>(this->config().localWriteCoalesceUs);
        if (queuedSinceUs <= 0 || elapsedUs < 0 || elapsedUs >= windowUs) {
            return;
        }
        _delayUs(static_cast<uint32_t>(windowUs - elapsedUs));
    }

    void _delayNoSlotBackoffIfNeeded() const {
        if (this->config().noSlotBackoffUs == 0) {
            return;
        }
        const auto remainingUs =
            _noSlotBackoffUntilUs - ::platform::get_time_us();
        if (remainingUs <= 0) {
            return;
        }
        _delayUs(static_cast<uint32_t>(remainingUs));
    }

    static void _delayUs(uint32_t delayUs) {
        if (delayUs == 0) {
            return;
        }
        const auto delayMs = (delayUs + 999U) / 1000U;
        ::platform::delay(::platform::ms_to_ticks(delayMs == 0 ? 1 : delayMs));
    }

    void _delayInterTurn() const {
        if (this->config().interTurnDelayMs > 0) {
            ::platform::delay(
                ::platform::ms_to_ticks(this->config().interTurnDelayMs));
        }
    }

    [[nodiscard]] bool
    _shouldWidenReceiveWindow(bool attentionRequested) const {
        if (this->config().attentionReceiveWindowBytes <=
            bucketBytes(BucketSize::B64)) {
            return false;
        }
        if (attentionRequested) {
            return true;
        }
        if (!_attention.configured()) {
            return _transceiver.hasQueuedFrames() || _hasQueuedWrites() ||
                   _hasPendingWrites();
        }
        return _transceiver.hasQueuedFrames() || _hasQueuedWrites();
    }

    ReturnCode _processQueuedWrites() {
        auto ret = OK();
        bool deferred = false;
        while (_hasQueuedWrites() && _findFreePendingWrite() != nullptr &&
               _transceiver.queuedTxBytes() <
                   this->config().maxOutboundSlotBytes) {
            ret.combine(_processOneWrite(deferred));
            if (!ret.ok() || deferred) {
                return ret;
            }
        }
        return ret;
    }

    ReturnCode _processOneWrite(bool &deferred) {
        deferred = false;
        TxQueueItem item{};
        if (_deferredWrite.has_value()) {
            item = *_deferredWrite;
            _deferredWrite.reset();
        } else {
            FAIL_IF_ERR_FWD(Totem::Queue::Platform::receive(_txQueue, &item, 0),
                            "Failed to receive SPI master write request");
        }
        if (!_transceiver.canFitNextFrame(
                item.request.data.size(),
                this->config().maxOutboundSlotBytes)) {
            if (!_transceiver.hasQueuedFrames()) {
                return item.request.nack(item.handle, ERR(CoreError, Overflow));
            }
            _deferredWrite = item;
            deferred = true;
            return OK();
        }
        auto sequenceResult = _transceiver.queueDataWithSequence(
            item.request.payloadType, item.request.data,
            FrameFlags::RequiresAck);
        if (!sequenceResult) {
            return item.request.nack(item.handle, sequenceResult.error());
        }
        auto *pending = _findFreePendingWrite();
        FAIL_IF_NULL(pending, ERR(CoreError, Overflow),
                     "SPI master pending write table is full");
        *pending = PendingWrite{
            .sequence = *sequenceResult,
            .handle = item.handle,
            .request = item.request,
            .length = item.request.data.size(),
            .sentAtMs = ::platform::get_time(),
            .sentAtUs = ::platform::get_time_us(),
            .occupied = true,
        };
        return OK();
    }

    ReturnCode _completePendingWrite(uint16_t sequence, ReturnCode result,
                                     int64_t receivedAtUs) {
        if (result.ok()) {
            return _completePendingWritesThrough(sequence, receivedAtUs);
        }
        auto *pending = _findPendingWrite(sequence);
        if (pending == nullptr) {
            return OK();
        }

        auto request = pending->request;
        const auto handle = pending->handle;
        *pending = {};

        return request.nack(handle, result);
    }

    ReturnCode _completePendingWritesThrough(uint16_t ackSequence,
                                             int64_t receivedAtUs) {
        auto ret = OK();
        for (auto &pending : _pendingWrites) {
            if (!pending.occupied ||
                !_sequenceAtOrBefore(pending.sequence, ackSequence)) {
                continue;
            }
            auto request = pending.request;
            const auto handle = pending.handle;
            const auto length = pending.length;
            const auto sentAtUs = pending.sentAtUs;
            pending = {};
            ret.combine(request.ack(handle, static_cast<uint16_t>(length),
                                    receivedAtUs, sentAtUs));
        }
        return ret;
    }

    ReturnCode _expirePendingWrites(uint32_t nowMs) {
        auto ret = OK();
        for (auto &pending : _pendingWrites) {
            if (!pending.occupied ||
                static_cast<uint32_t>(nowMs - pending.sentAtMs) <
                    pendingWriteTimeoutMs) {
                continue;
            }
            _log_v("SPI master write seq=%u timed out after %u ms",
                   pending.sequence,
                   static_cast<unsigned>(nowMs - pending.sentAtMs));
            ret.combine(
                pending.request.nack(pending.handle, ERR(CoreError, Timeout)));
            pending = {};
        }
        return ret;
    }

    ReturnCode _failPendingWrites(ReturnCode error) {
        auto ret = OK();
        for (auto &pending : _pendingWrites) {
            if (!pending.occupied) {
                continue;
            }
            ret.combine(pending.request.nack(pending.handle, error));
            pending = {};
        }
        return ret;
    }

    ReturnCode _failQueuedWrites(ReturnCode error) {
        auto ret = OK();
        if (_deferredWrite.has_value()) {
            ret.combine(
                _deferredWrite->request.nack(_deferredWrite->handle, error));
            _deferredWrite.reset();
        }
        if (_txQueue == nullptr) {
            _queuedWriteSinceUs.store(0, std::memory_order_relaxed);
            return ret;
        }
        TxQueueItem item{};
        while (Totem::Queue::Platform::receive(_txQueue, &item, 0).ok()) {
            ret.combine(item.request.nack(item.handle, error));
        }
        _queuedWriteSinceUs.store(0, std::memory_order_relaxed);
        return ret;
    }

    ReturnCode _resetLink(ReturnCode error) {
        metrics().addReset();
        const auto reason = link_recovery_reason_from_error(error);
        metrics().addLinkRecovery(reason);
        _log_w("SPI master link reset reason=%s error=" ERR_FMT,
               link_recovery_reason_name(reason), ERR_ARG(error));
        auto ret = OK();
        ret.combine(_failQueuedWrites(error));
        ret.combine(_failPendingWrites(error));
        _transceiver.reset();
        _transceiver.setAutoHelloResponse(false);
        _transceiver.registerAckCallback(this, _onAckFrame);
        ret.combine(_transceiver.queueHello());
        _lastHandshakeAttemptMs = ::platform::get_time();
        _nextHeartbeatAtMs = 0;
        _noSlotBackoffUntilUs = 0;
        _queuedWriteSinceUs.store(0, std::memory_order_relaxed);
        _missedHeartbeatResponses = 0;
        return ret;
    }

    [[nodiscard]] bool _heartbeatDue(uint32_t nowMs) const {
        return this->config().heartbeatEnabled && _transceiver.ready() &&
               static_cast<int32_t>(nowMs - _nextHeartbeatAtMs) >= 0;
    }

    void _recordPeerProgress(uint32_t nowMs) {
        _missedHeartbeatResponses = 0;
        _nextHeartbeatAtMs = nowMs + heartbeatIntervalMs;
    }

    ReturnCode _recordMissedHeartbeat() {
        _missedHeartbeatResponses++;
        _log_v("SPI master heartbeat missed count=%u",
               static_cast<unsigned>(_missedHeartbeatResponses));
        if (_missedHeartbeatResponses < heartbeatMissLimit) {
            return OK();
        }

        _log_w("Resetting SPI master link after %u missed heartbeats",
               static_cast<unsigned>(_missedHeartbeatResponses));
        return _resetLink(ERR(CoreError, Timeout));
    }

    PendingWrite *_findFreePendingWrite() {
        for (auto &pending : _pendingWrites) {
            if (!pending.occupied) {
                return &pending;
            }
        }
        return nullptr;
    }

    PendingWrite *_findPendingWrite(uint16_t sequence) {
        for (auto &pending : _pendingWrites) {
            if (pending.occupied && pending.sequence == sequence) {
                return &pending;
            }
        }
        return nullptr;
    }

    static bool _sequenceAtOrBefore(uint16_t sequence, uint16_t ackSequence) {
        return static_cast<uint16_t>(ackSequence - sequence) < 0x8000;
    }

    static constexpr uint32_t handshakeRetryMs = 500;
    static constexpr uint32_t heartbeatIntervalMs = 1000;
    static constexpr uint32_t pendingWriteTimeoutMs = 250;
    static constexpr uint8_t heartbeatMissLimit = 3;
    static constexpr size_t txQueueDepth = 8;
    static constexpr size_t pendingWriteDepth = 8;

    Platform::SpiMasterBus _bus{};
    Transceiver<4096> _transceiver{};
    DeviceHandle _device{};
    alignas(4) std::array<std::byte, 4096> _rxBuffer{};
    Totem::Queue::Handle _txQueue{};
    Totem::Queue::Platform::Storage<TxQueueItem, txQueueDepth>
        _txQueueStorage{};
    std::optional<TxQueueItem> _deferredWrite = std::nullopt;
    std::array<PendingWrite, pendingWriteDepth> _pendingWrites{};
    Totem::Wire::detail::AttentionLine _attention{};
    Totem::TaskController::RunnerKey _task = 0;
    std::atomic<bool> _attentionRequested{false};
    std::atomic<int64_t> _queuedWriteSinceUs{0};
    int64_t _noSlotBackoffUntilUs = 0;
    uint32_t _lastHandshakeAttemptMs = 0;
    uint32_t _nextHeartbeatAtMs = 0;
    uint32_t _turnCount = 0;
    uint8_t _missedHeartbeatResponses = 0;
    Totem::Wire::WriteRequestHandle _nextWriteHandle = 1;
};

inline constexpr LifecycleContract<Master, MasterConfig> _master_lifecycle;
inline constexpr TaskControllerContract<Master> _master_task_controller;
inline constexpr TaskController::TaskHooks::Contract<Master> _master_task_hook;

} // namespace Totem::Wire::Spi::detail
