#pragma once

#include "Base/HasTaskController.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Network/Interfaces/Endpoint.hpp"
#include "Network/detail/PlatformSelect.hpp"
#include "Network/detail/UdpSocket.hpp"
#include "Platform/PlatformSelect.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Wire.hpp"
#include "PubSubBackend/Transports/BaseTransport.hpp"
#include "PubSubBackend/detail/Metrics.hpp"
#include "PubSubBackend/detail/SerDe.hpp"
#include "Queue/Facade.hpp"
#include "StaticConfig/PubSubUdp.hpp"
#include "TaskController/Facade.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "TaskController/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include "Types/Signal.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <optional>
#include <span>

namespace Totem::PubSubBackend::Transports {

using NetworkReadyCallback = bool (*)(void *owner);

struct UdpTransportConfig {
    uint16_t localPort = StaticConfig::PubSubUdp::localPort;
    size_t rxQueueDepth = StaticConfig::PubSubUdp::rxQueueDepth;
    uint32_t receiveTimeoutMs = StaticConfig::PubSubUdp::receiveTimeoutMs;
    uint32_t keepaliveIntervalMs =
        StaticConfig::PubSubUdp::keepaliveIntervalMs;
    uint32_t peerTimeoutMs = StaticConfig::PubSubUdp::peerTimeoutMs;
    size_t txBurstLimit = StaticConfig::PubSubUdp::txBurstLimit;
    TaskController::Config task = StaticConfig::PubSubUdp::task;

    [[nodiscard]] ReturnCode validate() const {
        FAIL_IF(localPort == 0, ERR(CoreError, InvalidArgument),
                "UDP PubSub local port must be non-zero");
        FAIL_IF(rxQueueDepth == 0, ERR(CoreError, InvalidArgument),
                "UDP PubSub RX queue depth must be non-zero");
        FAIL_IF(receiveTimeoutMs == 0, ERR(CoreError, InvalidArgument),
                "UDP PubSub receive timeout must be non-zero");
        FAIL_IF(keepaliveIntervalMs == 0, ERR(CoreError, InvalidArgument),
                "UDP PubSub keepalive interval must be non-zero");
        FAIL_IF(peerTimeoutMs <= keepaliveIntervalMs,
                ERR(CoreError, InvalidArgument),
                "UDP PubSub peer timeout must exceed keepalive interval");
        FAIL_IF(txBurstLimit == 0, ERR(CoreError, InvalidArgument),
                "UDP PubSub TX burst limit must be non-zero");
        FAIL_IF(!task.validate(), ERR(CoreError, InvalidArgument),
                "UDP PubSub task config is invalid");
        return OK();
    }
};

struct UdpTransportDependencies {
    BaseTransportDependencies base;
    TaskController::IRegistry *taskRegistry = nullptr;
    UdpTransportConfig config{};
    void *networkReadyOwner = nullptr;
    NetworkReadyCallback networkReady = nullptr;

    [[nodiscard]] bool valid() const {
        return base.valid() && taskRegistry != nullptr;
    }

    BaseTransportDependencies
    withBaseDeps(void *ctx, SendCallback sendCallback = nullptr,
                 ReceiveCallback receiveCallback = nullptr) {
        if (this->base.transport == nullptr) {
            this->base.transport = ctx;
        }
        if (this->base.sendCallback == nullptr) {
            this->base.sendCallback = sendCallback;
        }
        if (this->base.receiveCallback == nullptr) {
            this->base.receiveCallback = receiveCallback;
        }
        return this->base;
    }
};

class UdpTransport : public BaseTransport,
                     protected HasTaskController<UdpTransport> {
    using Base = BaseTransport;
    friend TaskController::TaskHooks;
    friend class HasTaskController<UdpTransport>;
    friend struct TaskController::TaskHooks::Contract<UdpTransport>;
    friend struct TaskControllerContract<UdpTransport>;

    struct RxFrame {
        std::array<std::byte, Base::bufferSize> data{};
        size_t size = 0;
    };

  public:
    explicit UdpTransport(UdpTransportDependencies deps)
        : Base(_makeBaseDeps(this, deps)),
          HasTaskController<UdpTransport>(*deps.taskRegistry),
          _config(deps.config), _networkReadyOwner(deps.networkReadyOwner),
          _networkReady(deps.networkReady) {
        ABORT_IF_NOT(deps.valid(), "Invalid UdpTransport dependencies");
        ABORT_IF_ERR(_config.validate(), "Invalid UdpTransport config");
    }

    DELETE_COPY(UdpTransport)
    DELETE_MOVE(UdpTransport)

    static constexpr const char *name = "UdpTransport";
    static constexpr auto logComponent = PubSubBackend::detail::logComponent;

    ReturnCode begin() {
        detail::prewarmMetrics();
        FAIL_IF_ERR_FWD(_config.validate(),
                        "Invalid UDP PubSub transport config");
        _log_i("%s: bringup start; binding UDP port %u on all IPv4 interfaces",
               name, static_cast<unsigned>(_config.localPort));

        auto rxQueueResult =
            Totem::Queue::Platform::create(_rxFrameQueueStorage);
        if (!rxQueueResult) {
            FAIL(rxQueueResult.error(),
                 "Failed to create UDP PubSub RX queue");
        }
        _rxFrameQueue = *rxQueueResult;
        _log_i("%s: RX queue ready depth=%u", name,
               static_cast<unsigned>(_config.rxQueueDepth));
        auto txRawQueueResult =
            Totem::Queue::Platform::create(_txRawFrameQueueStorage);
        if (!txRawQueueResult) {
            (void)_destroyRxQueue();
            FAIL(txRawQueueResult.error(),
                 "Failed to create UDP PubSub raw TX queue");
        }
        _txRawFrameQueue = *txRawQueueResult;

        _log_i("%s: opening UDP socket 0.0.0.0:%u", name,
               static_cast<unsigned>(_config.localPort));
        auto openRet = _socket.open(_config.localPort);
        if (!openRet.ok()) {
            (void)_destroyRxQueue();
            (void)_destroyTxRawQueue();
            FAIL(openRet, "Failed to open UDP PubSub socket on port %u",
                 static_cast<unsigned>(_config.localPort));
        }
        _socketOpen.store(true, std::memory_order_release);
        _log_i("%s: UDP socket bound on all IPv4 interfaces port=%u", name,
               static_cast<unsigned>(_config.localPort));

        _log_i("%s: beginning PubSub base transport", name);
        auto baseRet = Base::begin();
        if (!baseRet.ok()) {
            (void)_cleanupStartedTransport(false);
            FAIL(baseRet, "Failed to begin UDP PubSub base transport");
        }
        _log_i("%s: PubSub base transport ready", name);

        auto hooks = TaskController::TaskHooks::bind(*this);
        _log_i("%s: beginning task controller", name);
        auto beginTaskRet = _beginTaskController();
        if (!beginTaskRet.ok()) {
            (void)_cleanupStartedTransport(true);
            FAIL(beginTaskRet,
                 "Failed to begin UDP PubSub transport task controller");
        }
        _log_i("%s: task controller ready", name);

        _log_i("%s: adding task %s", name, _config.task.name);
        auto taskResult =
            _taskController.addTask(_config.task.name, hooks);
        if (!taskResult) {
            (void)_cleanupStartedTransport(true);
            (void)_endTaskController();
            FAIL(taskResult.error(), "Failed to add UDP PubSub task");
        }
        _log_i("%s: task %s registered", name, _config.task.name);

        _log_i("%s: starting task %s", name, _config.task.name);
        auto startRet = _taskController.startTask(
            *taskResult, _taskConfig(_config.task));
        if (!startRet.ok()) {
            (void)_cleanupStartedTransport(true);
            (void)_endTaskController();
            FAIL(startRet, "Failed to start UDP PubSub task");
        }
        _log_i("%s: task %s started", name, _config.task.name);

        _taskKey = *taskResult;
        _log_i("%s: bringup complete; listening on UDP port %u on all IPv4 interfaces", name,
               static_cast<unsigned>(_config.localPort));
        return OK();
    }

    ReturnCode end() {
        _peerKnown.store(false, std::memory_order_release);
        auto ret = OK();
        if (_taskKey != 0) {
            _taskKey = 0;
            ret.combine(_endTaskController());
        }
        ret.combine(_cleanupStartedTransport(Base::active()));
        return ret;
    }

    ReturnCode
    send(size_t maxCount = std::numeric_limits<size_t>::max()) override {
        (void)maxCount;
        return _wakeTransportTask();
    }

    ReturnCode
    enqueueRaw(const Header &header, std::span<const std::byte> frame,
               const detail::TransportDispatch &dispatch = {}) override {
        (void)dispatch;
        FAIL_IF_ERR_FWD(_observeAvailability(),
                        "Failed to observe UDP transport availability");
        FAIL_IF_NOT(_available(), ERR(InvalidState),
                    "Cannot enqueue raw frame for unavailable UDP transport "
                    SV_FMT,
                    SV_ARG(instanceName()));
        detail::log_trace_packet("udp.transport.enqueueRaw", header,
                                 instanceName().data());
        return _enqueueTxRawFrame(frame);
    }

  private:
    [[nodiscard]] static BaseTransportDependencies
    _makeBaseDeps(UdpTransport *self, UdpTransportDependencies &deps) {
        auto baseDeps = deps.withBaseDeps(self, _sendCallback, _receiveCallback);
        baseDeps.availableCallback = _availableCallback;
        return baseDeps;
    }

    static bool _availableCallback(void *transport) {
        auto *self = static_cast<UdpTransport *>(transport);
        return self->_udpAvailable();
    }

    static ReturnCode _sendCallback(void *transport, const Header &header,
                                    std::span<const std::byte> frame) {
        auto *self = static_cast<UdpTransport *>(transport);
        return self->_send(header, frame);
    }

    static std::expected<size_t, ReturnCode>
    _receiveCallback(void *transport, std::span<std::byte> out) {
        auto *self = static_cast<UdpTransport *>(transport);
        return self->_receive(out);
    }

    [[nodiscard]] bool _udpAvailable() const {
        if (!_socketOpen.load(std::memory_order_acquire) || !_networkIsReady()) {
            return false;
        }
        const auto peer = _peerEndpoint();
        if (!peer.has_value()) {
            return false;
        }
        return !_peerStale(::platform::get_time());
    }

    [[nodiscard]] bool _networkIsReady() const {
        return _networkReady == nullptr || _networkReady(_networkReadyOwner);
    }

    std::optional<Network::Ipv4Endpoint> _peerEndpoint() const {
        if (!_peerKnown.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        auto endpoint = Network::Ipv4Endpoint{
            .address = _peerAddress.load(std::memory_order_relaxed),
            .port = _peerPort.load(std::memory_order_relaxed),
        };
        if (endpoint.address == 0 || endpoint.port == 0) {
            return std::nullopt;
        }
        return endpoint;
    }

    [[nodiscard]] bool _peerStale(uint32_t nowMs) const {
        if (!_peerKnown.load(std::memory_order_acquire)) {
            return false;
        }
        const auto lastSeen = _lastSeenMs.load(std::memory_order_relaxed);
        return static_cast<uint32_t>(nowMs - lastSeen) >=
               _config.peerTimeoutMs;
    }

    ReturnCode _send(const Header & /*header*/,
                     std::span<const std::byte> frame) {
        auto peer = _peerEndpoint();
        if (!peer.has_value() || _peerStale(::platform::get_time())) {
            detail::metrics().addUdpNoPeer();
            return ERR(CoreError, NotFound);
        }
        auto sendRet = _socket.sendTo(*peer, frame);
        if (!sendRet.ok()) {
            detail::metrics().addUdpFailure();
            return sendRet;
        }
        detail::metrics().addUdpTx();
        detail::metrics().addUdpTxBytes(frame.size());
        return OK();
    }

    std::expected<size_t, ReturnCode> _receive(std::span<std::byte> out) {
        if (_rxFrameQueue == nullptr) {
            return std::unexpected(ERR(CoreError, InvalidState));
        }
        RxFrame rxFrame{};
        auto receiveRet =
            Totem::Queue::Platform::receive(_rxFrameQueue, &rxFrame, 0);
        if (!receiveRet.ok()) {
            if (_isTimeout(receiveRet)) {
                return std::unexpected(ERR(CoreError, Timeout));
            }
            return std::unexpected(receiveRet);
        }
        if (out.size() < rxFrame.size) {
            return std::unexpected(ERR(CoreError, InvalidSize));
        }
        std::memcpy(out.data(), rxFrame.data.data(), rxFrame.size);
        return rxFrame.size;
    }

    ReturnCode _onTaskStep() {
        const auto nowMs = ::platform::get_time();
        _expirePeerIfNeeded(nowMs);
        if (!_socketOpen.load(std::memory_order_acquire) ||
            !_networkIsReadyLogged()) {
            return OK();
        }
        FAIL_IF_ERR_FWD(_sendQueuedFrames(),
                        "Failed to send queued UDP PubSub frames");

        RxFrame rxFrame{};
        Network::detail::ReceiveResult receiveResult{};
        auto receiveRet = _socket.receiveFrom(rxFrame.data,
                                              _config.receiveTimeoutMs,
                                              receiveResult);
        if (!receiveRet.ok()) {
            if (_isTimeout(receiveRet)) {
                FAIL_IF_ERR_FWD(_sendQueuedFrames(),
                                "Failed to send queued UDP PubSub frames");
                return _sendKeepaliveIfDue(::platform::get_time());
            }
            detail::metrics().addUdpFailure();
            _log_w("%s: UDP receive failed: " ERR_FMT, name,
                   ERR_ARG(receiveRet));
            FAIL_IF_ERR_FWD(_sendQueuedFrames(),
                            "Failed to send queued UDP PubSub frames");
            return _sendKeepaliveIfDue(::platform::get_time());
        }

        auto payload = std::span<const std::byte>{rxFrame.data.data(),
                                                  receiveResult.size};
        FAIL_IF_ERR_FWD(_handleDatagram(receiveResult.remote, payload,
                                        ::platform::get_time()),
                        "Failed to handle UDP PubSub datagram");
        FAIL_IF_ERR_FWD(_sendQueuedFrames(),
                        "Failed to send queued UDP PubSub frames");
        return _sendKeepaliveIfDue(::platform::get_time());
    }

    static ReturnCode _onTaskNotify(Signal /*signal*/) { return OK(); }

    ReturnCode _handleDatagram(Network::Ipv4Endpoint remote,
                               std::span<const std::byte> payload,
                               uint32_t nowMs) {
        if (_isKeepalive(payload)) {
            if (!_acceptPeer(remote, nowMs)) {
                detail::metrics().addUdpUnexpectedPeer();
                _log_w("%s: ignoring keepalive from unexpected UDP peer %s",
                       name, Network::detail::formatEndpoint(remote).c_str());
                return OK();
            }
            detail::metrics().addUdpKeepaliveRx();
            detail::metrics().addUdpRxBytes(payload.size());
            _log_d("%s: received UDP keepalive from %s", name,
                   Network::detail::formatEndpoint(remote).c_str());
            return OK();
        }

        auto headerResult = detail::SerDe::tryPeekHeader(payload);
        if (!headerResult) {
            detail::metrics().addUdpBadFrame();
            _log_w("%s: dropping invalid UDP PubSub frame from %s of "
                   "%zu bytes: " ERR_FMT,
                   name, Network::detail::formatEndpoint(remote).c_str(),
                   payload.size(), ERR_ARG(headerResult.error()));
            return OK();
        }

        auto validateRet =
            detail::SerDe::tryValidateFrame(payload, *headerResult);
        if (!validateRet.ok()) {
            detail::metrics().addUdpBadFrame();
            _log_w("%s: dropping corrupt UDP PubSub frame from %s of "
                   "%zu bytes: " ERR_FMT,
                   name, Network::detail::formatEndpoint(remote).c_str(),
                   payload.size(), ERR_ARG(validateRet));
            return OK();
        }

        if (!_acceptPeer(remote, nowMs)) {
            detail::metrics().addUdpUnexpectedPeer();
            _log_w("%s: dropping UDP PubSub frame from unexpected peer %s",
                   name, Network::detail::formatEndpoint(remote).c_str());
            return OK();
        }

        _log_i("%s: rx event t=%lu m=%lu src=%u n=%u", name,
               static_cast<unsigned long>(headerResult->topic),
               static_cast<unsigned long>(headerResult->messageId),
               static_cast<unsigned>(headerResult->source),
               static_cast<unsigned>(payload.size()));
        if (headerResult->topic ==
            static_cast<TopicId>(detail::Spec::Topic::PubSub)) {
            detail::metrics().addUdpControlFrame();
        }
        return _enqueueRxFrame(payload);
    }

    bool _acceptPeer(Network::Ipv4Endpoint remote, uint32_t nowMs) {
        auto peer = _peerEndpoint();
        if (!peer.has_value()) {
            _peerAddress.store(remote.address, std::memory_order_relaxed);
            _peerPort.store(remote.port, std::memory_order_relaxed);
            _lastSeenMs.store(nowMs, std::memory_order_relaxed);
            _nextKeepaliveMs.store(nowMs + _config.keepaliveIntervalMs,
                                   std::memory_order_relaxed);
            _peerKnown.store(true, std::memory_order_release);
            detail::metrics().addUdpPeerLearned();
            detail::metrics().addUdpAvailabilityChange();
            _log_i("%s: learned UDP PubSub peer %s", name,
                   Network::detail::formatEndpoint(remote).c_str());
            (void)_wake();
            return true;
        }

        if (peer->address != remote.address) {
            return false;
        }

        if (peer->port != remote.port) {
            _peerPort.store(remote.port, std::memory_order_relaxed);
            _log_i("%s: updated UDP PubSub peer port to %s", name,
                   Network::detail::formatEndpoint(remote).c_str());
        }
        _lastSeenMs.store(nowMs, std::memory_order_relaxed);
        return true;
    }

    ReturnCode _enqueueRxFrame(std::span<const std::byte> payload) {
        if (_rxFrameQueue == nullptr) {
            return ERR(CoreError, InvalidState);
        }
        if (payload.size() > Base::bufferSize) {
            detail::metrics().addUdpDrop();
            return OK();
        }

        RxFrame rxFrame{};
        std::memcpy(rxFrame.data.data(), payload.data(), payload.size());
        rxFrame.size = payload.size();

        auto sendRet =
            Totem::Queue::Platform::send(_rxFrameQueue, &rxFrame, 0);
        if (!sendRet.ok()) {
            detail::metrics().addUdpDrop();
            return OK();
        }

        detail::metrics().addUdpRx();
        detail::metrics().addUdpRxBytes(payload.size());
        return _wake();
    }

    ReturnCode _enqueueTxRawFrame(std::span<const std::byte> payload) {
        if (_txRawFrameQueue == nullptr) {
            return ERR(CoreError, InvalidState);
        }
        if (payload.size() > Base::bufferSize) {
            detail::metrics().addUdpDrop();
            return ERR(CoreError, Overflow);
        }

        RxFrame txFrame{};
        std::memcpy(txFrame.data.data(), payload.data(), payload.size());
        txFrame.size = payload.size();
        FAIL_IF_ERR_FWD(
            Totem::Queue::Platform::send(_txRawFrameQueue, &txFrame, 0),
            "Failed to enqueue raw UDP PubSub TX frame");
        return _wakeTransportTask();
    }

    ReturnCode _sendQueuedFrames() {
        auto ret = OK();
        ret.combine(Base::send(_config.txBurstLimit));
        ret.combine(_sendRawFrames(_config.txBurstLimit));
        return ret;
    }

    ReturnCode _sendRawFrames(size_t maxCount) {
        if (_txRawFrameQueue == nullptr) {
            return ERR(CoreError, InvalidState);
        }

        auto ret = OK();
        size_t count = 0;
        while (ret.ok() && count < maxCount) {
            RxFrame txFrame{};
            ret.combine(
                Totem::Queue::Platform::receive(_txRawFrameQueue, &txFrame, 0));
            if (ret.ok()) {
                auto frame = std::span<const std::byte>{txFrame.data.data(),
                                                        txFrame.size};
                auto sendRet = _send(Header{}, frame);
                if (!sendRet.ok()) {
                    _log_w("%s: dropping queued raw UDP PubSub frame of %zu "
                           "bytes after send failed: " ERR_FMT,
                           name, frame.size(), ERR_ARG(sendRet));
                }
                ++count;
            }
        }
        if (ret == ERR(CoreError, Timeout) || ret == ERR(Timeout)) {
            return OK();
        }
        return ret;
    }

    ReturnCode _wakeTransportTask() {
        if (_taskKey == 0) {
            return OK();
        }
        auto signalRet = _taskController.signalTaskDirect(_taskKey);
        if (!signalRet.ok()) {
            _log_w("%s: failed to wake UDP task: " ERR_FMT, name,
                   ERR_ARG(signalRet));
        }
        return OK();
    }

    ReturnCode _sendKeepaliveIfDue(uint32_t nowMs) {
        auto peer = _peerEndpoint();
        if (!peer.has_value()) {
            return OK();
        }
        const auto dueMs = _nextKeepaliveMs.load(std::memory_order_relaxed);
        if (static_cast<int32_t>(nowMs - dueMs) < 0) {
            return OK();
        }
        _nextKeepaliveMs.store(nowMs + _config.keepaliveIntervalMs,
                               std::memory_order_relaxed);
        auto frame = std::span<const std::byte>{keepalivePacket.data(),
                                                keepalivePacket.size()};
        auto sendRet = _socket.sendTo(*peer, frame);
        if (!sendRet.ok()) {
            detail::metrics().addUdpFailure();
            _log_w("%s: failed to send UDP keepalive to %s: " ERR_FMT, name,
                   Network::detail::formatEndpoint(*peer).c_str(),
                   ERR_ARG(sendRet));
            return OK();
        }
        detail::metrics().addUdpKeepaliveTx();
        detail::metrics().addUdpTxBytes(frame.size());
        _log_d("%s: sent UDP keepalive to %s", name,
               Network::detail::formatEndpoint(*peer).c_str());
        return OK();
    }

    [[nodiscard]] bool _networkIsReadyLogged() {
        const auto ready = _networkIsReady();
        if (!_networkReadyLogged || ready != _lastNetworkReady) {
            _networkReadyLogged = true;
            _lastNetworkReady = ready;
            if (ready) {
                _log_i("%s: network ready; UDP RX active on port %u", name,
                       static_cast<unsigned>(_config.localPort));
            } else {
                _log_i("%s: waiting for WiFi network readiness", name);
            }
        }
        return ready;
    }

    void _expirePeerIfNeeded(uint32_t nowMs) {
        if (!_peerKnown.load(std::memory_order_acquire) ||
            !_peerStale(nowMs)) {
            return;
        }
        auto peer = _peerEndpoint();
        _peerKnown.store(false, std::memory_order_release);
        _nextKeepaliveMs.store(0, std::memory_order_relaxed);
        detail::metrics().addUdpPeerReset();
        detail::metrics().addUdpAvailabilityChange();
        if (peer.has_value()) {
            _log_w("%s: UDP PubSub peer %s timed out", name,
                   Network::detail::formatEndpoint(*peer).c_str());
        } else {
            _log_w("%s: UDP PubSub peer timed out", name);
        }
        (void)_wake();
    }

    [[nodiscard]] static bool _isKeepalive(std::span<const std::byte> payload) {
        return payload.size() == keepalivePacket.size() &&
               std::equal(payload.begin(), payload.end(),
                          keepalivePacket.begin());
    }

    [[nodiscard]] static bool _isTimeout(ReturnCode ret) {
        return ret == ERR(CoreError, Timeout) || ret == ERR(Timeout);
    }

    ReturnCode _cleanupStartedTransport(bool baseStarted) {
        auto ret = OK();
        if (baseStarted) {
            ret.combine(Base::end());
        }
        if (_socketOpen.exchange(false, std::memory_order_acq_rel)) {
            ret.combine(_socket.close());
        }
        ret.combine(_destroyTxRawQueue());
        ret.combine(_destroyRxQueue());
        return ret;
    }

    ReturnCode _destroyRxQueue() {
        if (_rxFrameQueue == nullptr) {
            return OK();
        }
        auto ret = Totem::Queue::Platform::destroy(_rxFrameQueue);
        _rxFrameQueue = nullptr;
        return ret;
    }

    ReturnCode _destroyTxRawQueue() {
        if (_txRawFrameQueue == nullptr) {
            return OK();
        }
        auto ret = Totem::Queue::Platform::destroy(_txRawFrameQueue);
        _txRawFrameQueue = nullptr;
        return ret;
    }

    static constexpr std::array<std::byte, 8> keepalivePacket{
        std::byte{0x54}, std::byte{0x50}, std::byte{0x55}, std::byte{0x44},
        std::byte{0x50}, std::byte{0x4B}, std::byte{0x41}, std::byte{0x31},
    };

    UdpTransportConfig _config{};
    void *_networkReadyOwner = nullptr;
    NetworkReadyCallback _networkReady = nullptr;
    Network::detail::DefaultUdpSocket _socket{};
    std::atomic<bool> _socketOpen{false};
    std::atomic<bool> _peerKnown{false};
    std::atomic<uint32_t> _peerAddress{0};
    std::atomic<uint16_t> _peerPort{0};
    std::atomic<uint32_t> _lastSeenMs{0};
    std::atomic<uint32_t> _nextKeepaliveMs{0};
    TaskController::RunnerKey _taskKey = 0;
    bool _networkReadyLogged = false;
    bool _lastNetworkReady = false;

    Totem::Queue::Handle _rxFrameQueue{};
    Totem::Queue::Platform::Storage<RxFrame,
                                    StaticConfig::PubSubUdp::rxQueueDepth>
        _rxFrameQueueStorage{};
    Totem::Queue::Handle _txRawFrameQueue{};
    Totem::Queue::Platform::Storage<RxFrame,
                                    StaticConfig::PubSubUdp::rxQueueDepth>
        _txRawFrameQueueStorage{};
};

} // namespace Totem::PubSubBackend::Transports
