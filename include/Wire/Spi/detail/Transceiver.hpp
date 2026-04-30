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
        _helloResynced = false;
        _tx.reset(_peerId, _connectionId, _slotSequence.next(), BucketSize::B64,
                  SlotFlags::None, _lastReceivedSequence);
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
        _tx.reset(_peerId, _connectionId, _slotSequence.next(), bucket, flags,
                  _lastReceivedSequence);
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
        _prepareEmptySlotFor(SlotHeader::size + FrameHeader::size +
                             payload.size());
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
        _prepareEmptySlotFor(SlotHeader::size + FrameHeader::size +
                             payload.size());
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
        _prepareEmptySlotFor(SlotHeader::size + FrameHeader::size +
                             payload.size());
        FAIL_IF_ERR_FWD(_tx.appendFrame(FrameType::Response, payloadType,
                                        payload, _frameSequence.next(),
                                        responseTo, flags),
                        "Failed to append SPI response frame");
        metrics().addFrameTx();
        return OK();
    }

    std::span<const std::byte> finalizeTx() {
        auto bytes = _tx.finalize();
        log_trace_slot("tx", _tx.header());
        metrics().addTxBytes(static_cast<uint32_t>(bytes.size()));
        return bytes;
    }

    [[nodiscard]] const void *txData() const { return _tx.data(); }

    ReturnCode parseRx(std::span<const std::byte> bytes,
                       int64_t receivedAtUs = 0) {
        auto readerResult = SlotReader::parse(bytes);
        if (!readerResult) {
            if (_isEmptySlot(bytes) || ready()) {
                _log_i("SPI empty slot ignored len=%u",
                       static_cast<unsigned>(bytes.size()));
                beginSlot(BucketSize::B64);
                return OK();
            }
            if (readerResult.error() == ERR(WireError, CrcError)) {
                metrics().addCrcError();
            }
            _log_w("Invalid SPI slot header: first=%02x second=%02x len=%u "
                   "error=" ERR_FMT,
                   bytes.size() > 0 ? std::to_integer<unsigned>(bytes[0]) : 0,
                   bytes.size() > 1 ? std::to_integer<unsigned>(bytes[1]) : 0,
                   static_cast<unsigned>(bytes.size()),
                   ERR_ARG(readerResult.error()));
            return readerResult.error();
        }
        auto reader = *readerResult;
        auto header = reader.header();
        _helloResynced = false;
        log_trace_slot("rx", header);
        const bool containsHello = _slotContainsHello(reader);
        const auto expectedSequence = _receivedSlotSequence.current();
        if (_slotIsEmpty(header)) {
            _log_i("SPI empty protocol slot consumed seq=%u expected=%u",
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
                _log_i("SPI peer hello restart from slot sequence %u "
                       "expected %u",
                       header.sequence, expectedSequence);
                reset(header.peerId, header.connectionId);
                _receivedSlotSequence.reset(
                    static_cast<uint16_t>(header.sequence + 1));
                _lastReceivedSequence = header.sequence;
                _helloResynced = true;
            } else {
                _log_i("SPI stale hello consumed from slot sequence %u",
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
                _log_i("SPI hello resync from slot sequence %u expected %u",
                       header.sequence, expectedSequence);
                reset(header.peerId, header.connectionId);
                _receivedSlotSequence.reset(
                    static_cast<uint16_t>(header.sequence + 1));
                _lastReceivedSequence = header.sequence;
                _helloResynced = true;
            } else if (!sequenceOk) {
                if (_sequenceBehind(header.sequence, expectedSequence)) {
                    _log_w("Stale SPI slot ignored: got %u expected %u "
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
                _log_w("Missed SPI slot sequence: got %u expected %u "
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
                    return OK();
                }
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
    [[nodiscard]] bool hasPendingTx() const { return _tx.frameCount() > 0; }
    [[nodiscard]] bool consumeHelloResynced() {
        const auto value = _helloResynced;
        _helloResynced = false;
        return value;
    }
    [[nodiscard]] uint16_t lastReceivedSequence() const {
        return _lastReceivedSequence;
    }

  private:
    static bool _slotIsEmpty(const SlotHeader &header) {
        return header.frameCount == 0 && header.payloadBytes == 0 &&
               header.flags == SlotFlags::None;
    }

    static bool _isEmptySlot(std::span<const std::byte> bytes) {
        for (const auto byte : bytes) {
            if (byte != std::byte{0}) {
                return false;
            }
        }
        return true;
    }

    static bool _sequenceBehind(uint16_t sequence, uint16_t expected) {
        const auto distance = static_cast<uint16_t>(expected - sequence);
        return distance != 0 && distance < 0x8000;
    }

    void _prepareEmptySlotFor(size_t bytes) {
        if (_tx.frameCount() != 0 ||
            bytes <= static_cast<size_t>(_tx.header().bucketLength)) {
            return;
        }

        const auto header = _tx.header();
        _tx.reset(header.peerId, header.connectionId, header.sequence,
                  bucketFor(bytes), SlotFlags::None, _lastReceivedSequence);
    }

    ReturnCode _appendControl(FrameType type, SlotFlags flag) {
        _tx.addFlags(flag);
        FAIL_IF_ERR_FWD(_tx.appendFrame(type, PayloadType::Raw,
                                        std::span<const std::byte>{},
                                        _frameSequence.next()),
                        "Failed to append SPI control frame");
        metrics().addFrameTx();
        return OK();
    }

    ReturnCode _appendAck(FrameType type, uint16_t responseTo) {
        const auto flag =
            type == FrameType::Ack ? SlotFlags::Ack : SlotFlags::Nack;
        _tx.addFlags(flag);
        FAIL_IF_ERR_FWD(_tx.appendFrame(type, PayloadType::Raw,
                                        std::span<const std::byte>{},
                                        _frameSequence.next(), responseTo),
                        "Failed to append SPI ack frame");
        metrics().addFrameTx();
        return OK();
    }

    static bool _slotContainsHello(SlotReader reader) {
        while (true) {
            auto frameResult = reader.next();
            if (!frameResult) {
                return false;
            }
            if (frameResult->header.type == FrameType::Hello) {
                return true;
            }
        }
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
            return _handleAck(frame, OK(), receivedAtUs);
        case FrameType::Nack:
            return _handleAck(frame, ERR(CoreError, OperationFailed),
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

    ReturnCode _handleAck(const FrameView &frame, ReturnCode result,
                          int64_t receivedAtUs) {
        if (_ackCallback == nullptr) {
            return OK();
        }
        return _ackCallback(_ackOwner, frame.header.responseTo, result,
                            receivedAtUs);
    }

    ReturnCode _handleData(const FrameView &frame, int64_t receivedAtUs) {
        auto dispatchRet = _dispatchData(frame, receivedAtUs);
        if (!hasFlag(frame.header.flags, FrameFlags::RequiresAck)) {
            return dispatchRet;
        }

        if (dispatchRet.ok()) {
            return _appendAck(FrameType::Ack, frame.header.sequence);
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
        auto responseLength =
            handler->onRequest(handler->owner, frame.header.payloadType,
                               frame.payload, handler->response, receivedAtUs);
        if (!responseLength) {
            return responseLength.error();
        }
        auto response = handler->response.first(*responseLength);
        if (handler->onBeforeResponse != nullptr) {
            FAIL_IF_ERR_FWD(handler->onBeforeResponse(
                                handler->owner, frame.header.payloadType,
                                response, ::platform::get_time_us()),
                            "Failed to prepare SPI response payload");
        }
        return queueResponse(frame.header.payloadType, response,
                             frame.header.sequence);
    }

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
    bool _helloResynced = false;
    bool _preserveTxSlotSequenceOnReset = false;
};

} // namespace Totem::Wire::Spi::detail
