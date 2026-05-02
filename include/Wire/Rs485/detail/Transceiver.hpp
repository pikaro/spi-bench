#pragma once

#include "Generic/StateMachine.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Platform/platform/PlatformESP32/Uart.hpp"
#include "Types/Error.hpp"
#include "Types/Uart.hpp"
#include "Wire/Interfaces/Request.hpp"
#include "Wire/Rs485/detail/Pdu.hpp"
#include "Wire/Rs485/detail/Types.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace Totem::Wire::Rs485::detail {

constexpr std::array writeReadTurnTransitions{
    TRANSITION(Transceiver, WriteRequest, ReadReaction),
    TRANSITION(Transceiver, ReadReaction, ReadRequest),
    TRANSITION(Transceiver, ReadRequest, WriteReaction),
    TRANSITION(Transceiver, WriteReaction, WriteRequest),
};

constexpr std::array readWriteTurnTransitions{
    TRANSITION(Transceiver, ReadRequest, WriteReaction),
    TRANSITION(Transceiver, WriteReaction, WriteRequest),
    TRANSITION(Transceiver, WriteRequest, ReadReaction),
    TRANSITION(Transceiver, ReadReaction, ReadRequest),
};

template <TransceiverMode Mode> class TurnStateMachine {
    static constexpr auto Transitions = Mode == TransceiverMode::WriteRead
                                            ? writeReadTurnTransitions
                                            : readWriteTurnTransitions;
    using Machine =
        StateMachine<TransceiverState, TransceiverEvent, Transitions>;

  public:
    explicit TurnStateMachine(const char *ownerName)
        : _state(ownerName, startState()) {}

    [[nodiscard]] TransceiverState current() const { return _state.current(); }

    [[nodiscard]] bool canRead() const {
        const auto state = current();
        return state == TransceiverState::ReadRequest ||
               state == TransceiverState::ReadReaction;
    }

    [[nodiscard]] bool canInitiateWrite() const {
        return current() == TransceiverState::WriteRequest;
    }

    [[nodiscard]] bool canReadRequest() const {
        return current() == TransceiverState::ReadRequest;
    }

    [[nodiscard]] bool canReadReaction() const {
        return current() == TransceiverState::ReadReaction;
    }

    [[nodiscard]] bool canWriteReaction() const {
        return current() == TransceiverState::WriteReaction;
    }

    void reset() { _state.reset(startState()); }

    ReturnCode markWrite(FrameTurn turn) {
        const auto expected = turn == FrameTurn::Initiated
                                  ? TransceiverState::WriteRequest
                                  : TransceiverState::WriteReaction;
        FAIL_IF_NOT(_state.is(expected), ERR(CoreError, InvalidState),
                    "RS485 turn cannot write while in state " SV_FMT
                    "; expected " SV_FMT,
                    MAGIC_SV_ARG(_state.current()), MAGIC_SV_ARG(expected));
        return _state.step();
    }

    ReturnCode markWriteGrant() {
        FAIL_IF_NOT(_state.is(TransceiverState::WriteRequest),
                    ERR(CoreError, InvalidState),
                    "RS485 turn cannot grant while in state " SV_FMT,
                    MAGIC_SV_ARG(_state.current()));
        FAIL_IF_ERR_FWD(_state.step(), "Failed to advance grant write turn");
        return _state.step();
    }

    ReturnCode markRead(FrameType type) {
        const auto state = current();
        FAIL_IF(state != TransceiverState::ReadRequest &&
                    state != TransceiverState::ReadReaction,
                ERR(CoreError, InvalidState),
                "RS485 turn cannot read while in state %u",
                static_cast<unsigned>(state));
        FAIL_IF(type == FrameType::Grant &&
                    state != TransceiverState::ReadRequest,
                ERR(CoreError, InvalidState),
                "RS485 grant cannot be read while in state %u",
                static_cast<unsigned>(state));
        FAIL_IF_ERR_FWD(_state.step(), "Failed to advance read turn");
        if (type == FrameType::Grant) {
            return _state.step();
        }
        return OK();
    }

  private:
    static constexpr TransceiverState startState() {
        if constexpr (Mode == TransceiverMode::WriteRead) {
            return TransceiverState::WriteRequest;
        }
        return TransceiverState::ReadRequest;
    }

    Machine _state;
};

template <TransceiverMode Mode> class Transceiver {
  public:
    using BeforeWriteCallback = ReturnCode (*)(void *owner, int64_t sentAtUs);

    explicit Transceiver(const char *ownerName)
        : _ownerName(ownerName), _turn(ownerName) {}

    ReturnCode init(UartConfig uartConfig) {
        FAIL_IF(uartConfig.pins.txPin.has_value() &&
                    uartConfig.pins.rxPin.has_value() &&
                    uartConfig.pins.txPin == uartConfig.pins.rxPin,
                ERR(CoreError, InvalidArgument),
                "RS485 %s requires distinct UART TX and RX pins",
                _ownerName);
        FAIL_IF_ERR_FWD(_uart.init(uartConfig),
                        "Failed to initialize UART for RS485 %s", _ownerName);
        _uartConfig = uartConfig;
        _turn.reset();
        return OK();
    }

    ReturnCode deinit() {
        FAIL_IF_ERR_FWD(_uart.deinit(),
                        "Failed to deinitialize UART for RS485 %s", _ownerName);
        return OK();
    }

    ReturnCode registerUartCallback(void *owner, UartEventCallback callback) {
        return _uart.registerCallback(owner, callback);
    }

    ReturnCode sendFrame(FrameType type, PayloadType payloadType,
                         std::span<const std::byte> payload,
                         uint8_t responseTo = 0, Header *sentHeader = nullptr,
                         FrameTurn turn = FrameTurn::Initiated,
                         int64_t *sentAtUs = nullptr,
                         void *beforeWriteOwner = nullptr,
                         BeforeWriteCallback beforeWrite = nullptr) {
        FAIL_IF(payload.size() > UINT16_MAX, ERR(CoreError, InvalidArgument),
                "RS485 payload too large for %s", _ownerName);
        FAIL_IF_ERR_FWD(_turn.markWrite(turn),
                        "RS485 turn rejected write for %s", _ownerName);
        auto header =
            Header::make(type, payloadType,
                         static_cast<uint16_t>(payload.size()), responseTo);
        const auto writeStartUs = ::platform::get_time_us();
        if (beforeWrite != nullptr) {
            FAIL_IF_ERR_FWD(beforeWrite(beforeWriteOwner, writeStartUs),
                            "Failed RS485 before-write callback for %s",
                            _ownerName);
        }
        if (sentAtUs != nullptr) {
            *sentAtUs = writeStartUs;
        }
        FAIL_IF_ERR_FWD(writeHeader(header),
                        "Failed to write RS485 frame header for %s",
                        _ownerName);
        if (!payload.empty()) {
            FAIL_IF_ERR_FWD(_uart.write(payload),
                            "Failed to write RS485 frame payload for %s",
                            _ownerName);
        }
        FAIL_IF_ERR_FWD(
            _uart.waitTxComplete(_uartConfig.timeoutFromBytes(
                                     Header::headerSize + payload.size()) *
                                 2),
            "Failed to drain RS485 frame for %s", _ownerName);
        if (sentHeader != nullptr) {
            *sentHeader = header;
        }
        return OK();
    }

    ReturnCode sendControl(FrameType type, uint8_t responseTo = 0,
                           PayloadType payloadType = PayloadType::Raw,
                           Header *sentHeader = nullptr,
                           FrameTurn turn = FrameTurn::Initiated) {
        return sendFrame(type, payloadType, {}, responseTo, sentHeader, turn);
    }

    ReturnCode sendGrant(Header *sentHeader = nullptr) {
        FAIL_IF_ERR_FWD(_turn.markWriteGrant(),
                        "RS485 turn rejected grant for %s", _ownerName);
        auto header = Header::make(FrameType::Grant, PayloadType::Raw, 0, 0);
        FAIL_IF_ERR_FWD(writeHeader(header),
                        "Failed to write RS485 grant header for %s",
                        _ownerName);
        FAIL_IF_ERR_FWD(
            _uart.waitTxComplete(
                _uartConfig.timeoutFromBytes(Header::headerSize) * 2),
            "Failed to drain RS485 grant for %s", _ownerName);
        if (sentHeader != nullptr) {
            *sentHeader = header;
        }
        return OK();
    }

    std::expected<Header, ReturnCode> pollHeader() {
        FAIL_IF_NOT(_turn.canRead(),
                    std::unexpected(ERR(CoreError, InvalidState)),
                    "RS485 turn rejected poll for %s", _ownerName);
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            availableBytes, _uart.available(),
            "Failed to check RS485 header availability for %s", _ownerName);
        if (availableBytes < Header::headerSize) {
            return std::unexpected(ERR(CoreError, NotFound));
        }
        return receiveHeader();
    }

    std::expected<Header, ReturnCode> receiveHeader(uint32_t timeoutMs = 0) {
        FAIL_IF_NOT(_turn.canRead(),
                    std::unexpected(ERR(CoreError, InvalidState)),
                    "RS485 turn rejected read for %s", _ownerName);
        const auto effectiveTimeout =
            timeoutMs == 0
                ? _uartConfig.timeoutFromBytes(Header::headerSize) * 2
                : timeoutMs;
        auto readResult = _uart.readExact(
            std::as_writable_bytes(std::span(_headerBuf)), effectiveTimeout);
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            readBytes, readResult, "Failed to read RS485 frame header for %s",
            _ownerName);
        FAIL_IF(readBytes != Header::headerSize,
                std::unexpected(ERR(Corrupted)),
                "Incomplete RS485 frame header for %s", _ownerName);
        auto headerResult = Header::fromBytes(_headerBuf);
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            header, headerResult, "Failed to parse RS485 frame header for %s",
            _ownerName);
        FAIL_IF_ERR_FWD_UNEXPECTED(_turn.markRead(header.type),
                                   "Failed to advance RS485 read turn for %s",
                                   _ownerName);
        return header;
    }

    std::expected<uint16_t, ReturnCode>
    receivePayload(const Header &header, std::span<std::byte> buffer) {
        FAIL_IF(header.payloadLength > buffer.size(),
                std::unexpected(ERR(CoreError, InvalidArgument)),
                "RS485 payload length %u exceeds read buffer size %zu for %s",
                header.payloadLength, buffer.size(), _ownerName);
        if (header.payloadLength == 0) {
            return 0;
        }
        auto payload = buffer.first(header.payloadLength);
        auto dataReadResult = _uart.readExact(
            payload, _uartConfig.timeoutFromBytes(header.payloadLength) * 2);
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            dataReadBytes, dataReadResult,
            "Failed to read RS485 frame payload for %s", _ownerName);
        FAIL_IF(dataReadBytes != header.payloadLength,
                std::unexpected(ERR(Corrupted)),
                "Incomplete RS485 frame payload for %s", _ownerName);
        return static_cast<uint16_t>(dataReadBytes);
    }

    ReturnCode discardRx() { return _uart.discardRx(); }

    [[nodiscard]] TransceiverState turnState() const { return _turn.current(); }

    [[nodiscard]] bool canRead() const { return _turn.canRead(); }
    [[nodiscard]] bool canReadRequest() const { return _turn.canReadRequest(); }
    [[nodiscard]] bool canInitiateWrite() const {
        return _turn.canInitiateWrite();
    }
    [[nodiscard]] bool canWriteReaction() const {
        return _turn.canWriteReaction();
    }
    void resetTurn() { _turn.reset(); }

  private:
    ReturnCode writeHeader(const Header &header) {
        _headerBuf = header.toBytes();
        return _uart.write(std::as_bytes(std::span(_headerBuf)));
    }

    platform::Uart _uart;
    UartConfig _uartConfig{};
    std::array<std::uint8_t, Header::headerSize> _headerBuf{};
    const char *_ownerName;
    TurnStateMachine<Mode> _turn;
};

} // namespace Totem::Wire::Rs485::detail
