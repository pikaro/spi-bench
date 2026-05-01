#pragma once

#include "Generic/StateMachine.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Types/Error.hpp"
#include "Wire/Interfaces/Request.hpp"
#include "Wire/Spi/detail/Metrics.hpp"
#include "Wire/Spi/detail/Pdu.hpp"
#include "Wire/Spi/detail/SlotBuffer.hpp"
#include "Wire/Spi/detail/Trace.hpp"
#include "Wire/detail/Sequence.hpp"
#include <array>
#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Totem::Wire::Spi::detail {

enum class LinkState : uint8_t {
    Invalid = 0,
    Initial,
    HelloSent,
    Ready,
};

enum class LinkEvent : uint8_t {
    Default = 0,
    SendHello,
    RetryHello,
    ReceiveHello,
};

constexpr std::array linkStateTransitions{
    TRANSITION(Link, Initial, HelloSent, SendHello),
    TRANSITION(Link, HelloSent, HelloSent, RetryHello),
    TRANSITION(Link, Initial, Ready, ReceiveHello),
    TRANSITION(Link, HelloSent, Ready, ReceiveHello),
    TRANSITION(Link, Ready, Ready, ReceiveHello),
};

using LinkStateMachine =
    StateMachine<LinkState, LinkEvent, linkStateTransitions>;

template <size_t Capacity, size_t MaxHandlers = 4> class Transceiver {
  public:
    using ResponseCallback = ReturnCode (*)(void *owner,
                                            const FrameView &frame,
                                            int64_t receivedAtUs);
    using AckCallback = ReturnCode (*)(void *owner, uint16_t sequence,
                                       ReturnCode result,
                                       int64_t receivedAtUs);

    void reset(uint8_t peerId = 0, uint8_t connectionId = 1) {
        _peerId = peerId;
        _connectionId = connectionId;
        _state.reset(LinkState::Initial);
        if (!_preserveTxSlotSequenceOnReset) {
            _slotSequence.reset(1);
        }
        _frameSequence.reset(1);
        _receivedSlotSequence.reset(1);
        _lastReceivedSequence = 0;
        _lastAckRequiredFrameSequence = 0;
        _lastTransmittedAckRequiredFrameSequence = 0;
        _attentionAssertedAtUs.store(0, std::memory_order_release);
        _attentionAssertedPending.store(false, std::memory_order_release);
        _helloResynced = false;
        _tx.reset(_peerId, _connectionId, _slotSequence.next(), BucketSize::B64,
                  SlotFlags::None);
    }

    void setAutoHelloResponse(bool enabled) { _autoHelloResponse = enabled; }
    void setPreserveTxSlotSequenceOnReset(bool enabled) {
        _preserveTxSlotSequenceOnReset = enabled;
    }

    ReturnCode registerHandler(const FrameHandler &handler) {
        FAIL_IF(!handler.validate(), ERR(CoreError, InvalidArgument),
                "Invalid SPI frame handler");
        for (size_t index = 0; index < _handlerCount; index++) {
            FAIL_IF(_handlers[index].payloadType == handler.payloadType,
                    ERR(CoreError, AlreadyExists),
                    "SPI handler already registered");
        }
        FAIL_IF(_handlerCount >= _handlers.size(), ERR(CoreError, Overflow),
                "SPI handler table full");
        _handlers[_handlerCount++] = handler;
        return OK();
    }

    void registerResponseCallback(void *owner, ResponseCallback callback) {
        _responseOwner = owner;
        _responseCallback = callback;
    }

    void registerAckCallback(void *owner, AckCallback callback) {
        _ackOwner = owner;
        _ackCallback = callback;
    }

    void beginSlot(BucketSize bucket, SlotFlags flags = SlotFlags::None) {
        _tx.reset(_peerId, _connectionId, _slotSequence.next(), bucket, flags);
    }

    void advanceTxSlot(BucketSize bucket = BucketSize::B64,
                       SlotFlags flags = SlotFlags::None) {
        beginSlot(bucket, flags);
    }

    ReturnCode queueHello() {
        if (_state.is(LinkState::Initial)) {
            FAIL_IF_ERR_FWD(_state.transition(LinkEvent::SendHello),
                            "Failed to enter SPI hello-sent state");
        } else if (_state.is(LinkState::HelloSent)) {
            FAIL_IF_ERR_FWD(_state.transition(LinkEvent::RetryHello),
                            "Failed to retry SPI hello");
        }
        FAIL_IF(!_state.is(LinkState::HelloSent), ERR(CoreError, InvalidState),
                "SPI hello can only be queued before link ready");
        _tx.clear(SlotFlags::Hello);
        return _appendControl(FrameType::Hello, SlotFlags::Hello);
    }

    ReturnCode queueHeartbeat() {
        return _appendControl(FrameType::Heartbeat, SlotFlags::Heartbeat);
    }

    ReturnCode ensureTxWindow(size_t bytes) {
        return _tx.ensureBucketFor(bytes);
    }

    [[nodiscard]] size_t queuedTxBytes() const {
        return _tx.header().slotLength;
    }

    [[nodiscard]] bool hasQueuedFrames() const {
        return _tx.frameCount() > 0;
    }

    [[nodiscard]] bool canFitNextFrame(size_t payloadBytes,
                                       size_t maxSlotBytes) const {
        return bucketBytes(bucketFor(_requiredBytesForNextFrame(
                   payloadBytes))) <= maxSlotBytes;
    }

    ReturnCode queueData(PayloadType payloadType,
                         std::span<const std::byte> payload,
                         FrameFlags flags = FrameFlags::None) {
        auto result = queueDataWithSequence(payloadType, payload, flags);
        if (!result) {
            return result.error();
        }
        return OK();
    }

    std::expected<uint16_t, ReturnCode>
    queueDataWithSequence(PayloadType payloadType,
                          std::span<const std::byte> payload,
                          FrameFlags flags = FrameFlags::None) {
        FAIL_IF_ERR_FWD_UNEXPECTED(
            _ensureCapacityForNextFrame(payload.size()),
            "Failed to grow SPI data slot");
        const auto sequence = _frameSequence.next();
        auto ret = _tx.appendFrame(FrameType::Data, payloadType, payload,
                                   sequence, 0, flags);
        if (!ret.ok()) {
            return std::unexpected(ret);
        }
        metrics().addFrameTx();
        return sequence;
    }

    ReturnCode queueRequest(PayloadType payloadType,
                            std::span<const std::byte> payload,
                            FrameFlags flags = FrameFlags::None) {
        FAIL_IF_ERR_FWD(_ensureCapacityForNextFrame(payload.size()),
                        "Failed to grow SPI request slot");
        FAIL_IF_ERR_FWD(_tx.appendFrame(FrameType::Request, payloadType,
                                        payload, _frameSequence.next(), 0,
                                        flags),
                        "Failed to append SPI request frame");
        metrics().addFrameTx();
        return OK();
    }

    ReturnCode queueResponse(PayloadType payloadType,
                             std::span<const std::byte> payload,
                             uint16_t responseTo,
                             FrameFlags flags = FrameFlags::None) {
        FAIL_IF_ERR_FWD(_ensureCapacityForNextFrame(payload.size()),
                        "Failed to grow SPI response slot");
        FAIL_IF_ERR_FWD(_tx.appendFrame(FrameType::Response, payloadType,
                                        payload, _frameSequence.next(),
                                        responseTo, flags),
                        "Failed to append SPI response frame");
        metrics().addFrameTx();
        return OK();
    }

    std::span<const std::byte> finalizeTx() {
        if (_hasPendingHeaderAck()) {
            _tx.setAckSequence(_lastAckRequiredFrameSequence);
            _tx.addFlags(SlotFlags::Ack);
        } else {
            _tx.setAckSequence(0);
        }
        auto bytes = _tx.finalize();
        log_trace_slot("tx", _tx.header());
        metrics().addTxBytes(static_cast<uint32_t>(bytes.size()));
        return bytes;
    }

    [[nodiscard]] const void *txData() const { return _tx.data(); }

    [[nodiscard]] static bool hasProtocolPreamble(
        std::span<const std::byte> bytes) {
        return _hasProtocolPreamble(bytes);
    }

    void markTxConsumed() {
        if (hasFlag(_tx.header().flags, SlotFlags::Ack)) {
            _lastTransmittedAckRequiredFrameSequence =
                _tx.header().ackSequence;
        }
    }

    void recordAttentionAsserted(int64_t assertedAtUs) {
        if (assertedAtUs == 0) {
            return;
        }
        _attentionAssertedAtUs.store(assertedAtUs,
                                     std::memory_order_release);
        _attentionAssertedPending.store(true, std::memory_order_release);
    }

    ReturnCode parseRx(std::span<const std::byte> bytes,
                       int64_t receivedAtUs = 0) {
        auto readerResult = SlotReader::parse(bytes);
        if (!readerResult) {
            if (_isNoSlotObservation(bytes)) {
                metrics().addNoSlot();
                _log_v("SPI no-slot observation ignored len=%u first=%02x "
                       "second=%02x",
                       static_cast<unsigned>(bytes.size()), _byteAt(bytes, 0),
                       _byteAt(bytes, 1));
                return OK();
            }
            if (readerResult.error() == ERR(WireError, CrcError)) {
                metrics().addCrcError();
            }
            metrics().addBadSlot();
            _log_v("Invalid SPI slot header: first=%02x second=%02x len=%u "
                   "error=" ERR_FMT,
                   _byteAt(bytes, 0), _byteAt(bytes, 1),
                   static_cast<unsigned>(bytes.size()),
                   ERR_ARG(readerResult.error()));
            return readerResult.error();
        }
        auto reader = *readerResult;
        auto header = reader.header();
        _helloResynced = false;
        log_trace_slot("rx", header);
        const bool containsHello = hasFlag(header.flags, SlotFlags::Hello);
        const auto expectedSequence = _receivedSlotSequence.current();
        if (_slotIsEmpty(header)) {
            metrics().addEmptySlot();
            _log_v("SPI empty protocol slot consumed seq=%u expected=%u",
                   header.sequence, expectedSequence);
            if (ready()) {
                _receivedSlotSequence.reset(
                    static_cast<uint16_t>(header.sequence + 1));
                _lastReceivedSequence = header.sequence;
            }
            metrics().addSlotRx();
            metrics().addRxBytes(header.slotLength);
            beginSlot(BucketSize::B64);
            return OK();
        }
        if (ready() && containsHello) {
            if (_sequenceBehind(header.sequence, expectedSequence)) {
                metrics().addHelloResync();
                _log_v("SPI peer hello restart from slot sequence %u "
                       "expected %u",
                       header.sequence, expectedSequence);
                reset(header.peerId, header.connectionId);
                _receivedSlotSequence.reset(
                    static_cast<uint16_t>(header.sequence + 1));
                _lastReceivedSequence = header.sequence;
                _helloResynced = true;
            } else {
                _log_v("SPI stale hello consumed from slot sequence %u",
                       header.sequence);
                metrics().addSlotRx();
                metrics().addRxBytes(header.slotLength);
                beginSlot(BucketSize::B64);
                return OK();
            }
        }
        if (!_helloResynced) {
            const bool sequenceOk =
                _receivedSlotSequence.received(header.sequence);
            if (containsHello && !sequenceOk) {
                metrics().addHelloResync();
                _log_v("SPI hello resync from slot sequence %u expected %u",
                       header.sequence, expectedSequence);
                reset(header.peerId, header.connectionId);
                _receivedSlotSequence.reset(
                    static_cast<uint16_t>(header.sequence + 1));
                _lastReceivedSequence = header.sequence;
                _helloResynced = true;
            } else if (!sequenceOk) {
                if (_sequenceBehind(header.sequence, expectedSequence)) {
                    metrics().addStaleSequence();
                    _log_v("Stale SPI slot ignored: got %u expected %u "
                           "frames=%u flags=0x%04X payload=%u",
                           header.sequence, expectedSequence,
                           header.frameCount,
                           static_cast<unsigned>(header.flags),
                           header.payloadBytes);
                    metrics().addSlotRx();
                    metrics().addRxBytes(header.slotLength);
                    beginSlot(BucketSize::B64);
                    return OK();
                }
                metrics().addMissedSequence();
                _log_v("Missed SPI slot sequence: got %u expected %u "
                       "frames=%u flags=0x%04X payload=%u",
                       header.sequence, expectedSequence, header.frameCount,
                       static_cast<unsigned>(header.flags),
                       header.payloadBytes);
                _receivedSlotSequence.reset(
                    static_cast<uint16_t>(header.sequence + 1));
            }
        }
        _lastReceivedSequence = header.sequence;
        metrics().addSlotRx();
        metrics().addRxBytes(header.slotLength);
        beginSlot(BucketSize::B64);

        while (true) {
            auto frameResult = reader.next();
            if (!frameResult) {
                if (frameResult.error() == ERR(CoreError, NotFound)) {
                    if (hasFlag(header.flags, SlotFlags::Ack)) {
                        FAIL_IF_ERR_FWD(_handleAck(header.ackSequence, OK(),
                                                   receivedAtUs),
                                        "Failed to handle SPI header ack");
                    }
                    return OK();
                }
                metrics().addBadSlot();
                _log_w("Invalid SPI frame in slot seq=%u frameIndex=%u/%u "
                       "payloadBytes=%u: " ERR_FMT,
                       header.sequence, reader.framesRead(),
                       header.frameCount, header.payloadBytes,
                       ERR_ARG(frameResult.error()));
                return frameResult.error();
            }
            FAIL_IF_ERR_FWD(_handleFrame(*frameResult, receivedAtUs),
                            "Failed to handle SPI frame");
        }
    }

    [[nodiscard]] LinkState state() const { return _state.current(); }
    [[nodiscard]] bool ready() const { return _state.is(LinkState::Ready); }
    [[nodiscard]] bool hasPendingTx() const {
        return _tx.frameCount() > 0 || _hasPendingHeaderAck();
    }
    [[nodiscard]] bool hasPendingHeaderAck() const {
        return _hasPendingHeaderAck();
    }
    [[nodiscard]] bool consumeHelloResynced() {
        const auto value = _helloResynced;
        _helloResynced = false;
        return value;
    }
    [[nodiscard]] uint16_t lastReceivedSequence() const {
        return _lastReceivedSequence;
    }

  private:
    static unsigned _byteAt(std::span<const std::byte> bytes, size_t index) {
        return index < bytes.size() ? std::to_integer<unsigned>(bytes[index])
                                    : 0;
    }

    static bool _slotIsEmpty(const SlotHeader &header) {
        return header.frameCount == 0 && header.payloadBytes == 0 &&
               header.flags == SlotFlags::None && header.ackSequence == 0;
    }

    static bool _isNoSlotObservation(std::span<const std::byte> bytes) {
        return !_hasProtocolPreamble(bytes);
    }

    static bool _hasProtocolPreamble(std::span<const std::byte> bytes) {
        return bytes.size() >= 2 &&
               std::to_integer<uint8_t>(bytes[0]) == SlotHeader::preamble &&
               std::to_integer<uint8_t>(bytes[1]) == SlotHeader::version;
    }

    static bool _sequenceBehind(uint16_t sequence, uint16_t expected) {
        const auto distance = static_cast<uint16_t>(expected - sequence);
        return distance != 0 && distance < 0x8000;
    }

    [[nodiscard]] bool _hasPendingHeaderAck() const {
        return _lastAckRequiredFrameSequence !=
               _lastTransmittedAckRequiredFrameSequence;
    }

    ReturnCode _ensureCapacityForNextFrame(size_t payloadBytes) {
        return _tx.ensureBucketFor(_requiredBytesForNextFrame(payloadBytes));
    }

    [[nodiscard]] size_t _requiredBytesForNextFrame(size_t payloadBytes) const {
        return SlotHeader::size + _tx.header().payloadBytes +
               FrameHeader::size + payloadBytes;
    }

    ReturnCode _appendControl(FrameType type, SlotFlags flag) {
        FAIL_IF_ERR_FWD(_ensureCapacityForNextFrame(0),
                        "Failed to grow SPI control slot");
        _tx.addFlags(flag);
        FAIL_IF_ERR_FWD(_tx.appendFrame(type, PayloadType::Raw,
                                        std::span<const std::byte>{},
                                        _frameSequence.next()),
                        "Failed to append SPI control frame");
        metrics().addFrameTx();
        return OK();
    }

    ReturnCode _appendAck(FrameType type, uint16_t responseTo) {
        FAIL_IF_ERR_FWD(_ensureCapacityForNextFrame(0),
                        "Failed to grow SPI ack slot");
        if (type == FrameType::Nack) {
            _tx.addFlags(SlotFlags::Nack);
        }
        FAIL_IF_ERR_FWD(_tx.appendFrame(type, PayloadType::Raw,
                                        std::span<const std::byte>{},
                                        _frameSequence.next(), responseTo),
                        "Failed to append SPI ack frame");
        metrics().addFrameTx();
        return OK();
    }

    ReturnCode _handleFrame(const FrameView &frame, int64_t receivedAtUs) {
        log_trace_frame("rx", frame.header);
        metrics().addFrameRx();
        switch (frame.header.type) {
        case FrameType::Hello:
            return _handleHello();
        case FrameType::Heartbeat:
        case FrameType::Status:
        case FrameType::Nop:
            return OK();
        case FrameType::Ack:
            return _handleAck(frame.header.responseTo, OK(), receivedAtUs);
        case FrameType::Nack:
            return _handleAck(frame.header.responseTo,
                              ERR(CoreError, OperationFailed),
                              receivedAtUs);
        case FrameType::Data:
            return _handleData(frame, receivedAtUs);
        case FrameType::Request:
            return _dispatchRequest(frame, receivedAtUs);
        case FrameType::Response:
            if (_responseCallback != nullptr) {
                return _responseCallback(_responseOwner, frame, receivedAtUs);
            }
            return _dispatchData(frame, receivedAtUs);
        default:
            return ERR(CoreError, InvalidData);
        }
    }

    ReturnCode _markHelloReceived() {
        return _state.transition(LinkEvent::ReceiveHello);
    }

    ReturnCode _handleHello() {
        const bool wasReady = ready();
        FAIL_IF_ERR_FWD(_markHelloReceived(), "Failed to handle SPI hello");
        if (_autoHelloResponse && !wasReady) {
            FAIL_IF_ERR_FWD(_appendControl(FrameType::Hello, SlotFlags::Hello),
                            "Failed to queue SPI hello response");
        }
        return OK();
    }

    ReturnCode _handleAck(uint16_t sequence, ReturnCode result,
                          int64_t receivedAtUs) {
        if (_ackCallback == nullptr) {
            return OK();
        }
        return _ackCallback(_ackOwner, sequence, result, receivedAtUs);
    }

    ReturnCode _handleData(const FrameView &frame, int64_t receivedAtUs) {
        auto dispatchRet = _dispatchData(frame, receivedAtUs);
        if (!hasFlag(frame.header.flags, FrameFlags::RequiresAck)) {
            return dispatchRet;
        }

        if (dispatchRet.ok()) {
            _lastAckRequiredFrameSequence = frame.header.sequence;
            return OK();
        }

        _log_w("SPI data frame seq=%u payload=%u failed, sending nack: "
               ERR_FMT,
               frame.header.sequence,
               static_cast<unsigned>(frame.header.payloadType),
               ERR_ARG(dispatchRet));
        FAIL_IF_ERR_FWD(_appendAck(FrameType::Nack, frame.header.sequence),
                        "Failed to queue SPI data nack");
        return OK();
    }

    ReturnCode _dispatchData(const FrameView &frame, int64_t receivedAtUs) {
        auto *handler = _handlerFor(frame.header.payloadType);
        if (handler == nullptr || handler->onData == nullptr) {
            return OK();
        }
        return handler->onData(handler->owner, frame.header.payloadType,
                               frame.payload, receivedAtUs);
    }

    ReturnCode _dispatchRequest(const FrameView &frame, int64_t receivedAtUs) {
        auto *handler = _handlerFor(frame.header.payloadType);
        if (handler == nullptr || handler->onRequest == nullptr) {
            return OK();
        }
        const auto requestReceivedAtUs =
            _receivedAtForRequest(frame, receivedAtUs);
        auto responseLength =
            handler->onRequest(handler->owner, frame.header.payloadType,
                               frame.payload, handler->response,
                               requestReceivedAtUs);
        if (!responseLength) {
            return responseLength.error();
        }
        auto response = handler->response.first(*responseLength);
        _log_v("SPI request payload=%u seq=%u queued response bytes=%u",
               static_cast<unsigned>(frame.header.payloadType),
               frame.header.sequence, static_cast<unsigned>(response.size()));
        FAIL_IF_ERR_FWD(_ensureCapacityForNextFrame(response.size()),
                        "Failed to grow SPI response slot");
        FAIL_IF_ERR_FWD(_tx.appendFrame(FrameType::Response,
                                        frame.header.payloadType, response,
                                        _frameSequence.next(),
                                        frame.header.sequence,
                                        FrameFlags::None),
                        "Failed to append SPI response frame");
        metrics().addFrameTx();
        return OK();
    }

    [[nodiscard]] int64_t _receivedAtForRequest(const FrameView &frame,
                                                int64_t receivedAtUs) {
        if (!hasFlag(frame.header.flags, FrameFlags::AttentionSync)) {
            return receivedAtUs;
        }
        if (!_attentionAssertedPending.exchange(false,
                                                std::memory_order_acq_rel)) {
            _log_v("SPI attention-sync request had no pending attention edge");
            return 0;
        }
        const auto assertedAtUs =
            _attentionAssertedAtUs.exchange(0, std::memory_order_acq_rel);
        if (assertedAtUs == 0) {
            _log_v("SPI attention-sync request had zero attention timestamp");
            return 0;
        }
        const auto ageUs = receivedAtUs - assertedAtUs;
        if (receivedAtUs < assertedAtUs || ageUs > attentionSyncMaxAgeUs) {
            _log_v("SPI attention-sync marker stale age=%" PRId64
                   " us max=%" PRId64 " us",
                   ageUs, attentionSyncMaxAgeUs);
            return 0;
        }

        return assertedAtUs;
    }

    static constexpr int64_t attentionSyncMaxAgeUs = 2500;

    FrameHandler *_handlerFor(PayloadType payloadType) {
        for (size_t index = 0; index < _handlerCount; index++) {
            if (_handlers[index].payloadType == payloadType) {
                return &_handlers[index];
            }
        }
        return nullptr;
    }

    SlotBuffer<Capacity> _tx{};
    std::array<FrameHandler, MaxHandlers> _handlers{};
    size_t _handlerCount = 0;
    uint8_t _peerId = 0;
    uint8_t _connectionId = 1;
    LinkStateMachine _state{"Spi::Transceiver", LinkState::Initial,
                            LinkState::Ready};
    bool _autoHelloResponse = false;
    void *_responseOwner = nullptr;
    ResponseCallback _responseCallback = nullptr;
    void *_ackOwner = nullptr;
    AckCallback _ackCallback = nullptr;
    Totem::Wire::detail::Sequence<uint16_t> _slotSequence{};
    Totem::Wire::detail::Sequence<uint16_t> _frameSequence{};
    Totem::Wire::detail::Sequence<uint16_t> _receivedSlotSequence{};
    uint16_t _lastReceivedSequence = 0;
    uint16_t _lastAckRequiredFrameSequence = 0;
    uint16_t _lastTransmittedAckRequiredFrameSequence = 0;
    std::atomic<int64_t> _attentionAssertedAtUs{0};
    bool _helloResynced = false;
    std::atomic<bool> _attentionAssertedPending{false};
    bool _preserveTxSlotSequenceOnReset = false;
};

} // namespace Totem::Wire::Spi::detail
