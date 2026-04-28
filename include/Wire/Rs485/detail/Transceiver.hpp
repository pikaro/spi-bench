#pragma once

#include "Generic/StateMachine.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Types/Error.hpp"
#include "Types/Uart.hpp"
#include "Wire/Interfaces/Request.hpp"
#include "Wire/Rs485/detail/Pdu.hpp"
#include "Wire/Rs485/detail/Types.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>

namespace Totem::Wire::Rs485::detail {

constexpr std::array writeReadTransitions{
    TRANSITION(TransceiverState, Initial, Sleeping),
    TRANSITION(TransceiverState, Sleeping, Writing),
    TRANSITION(TransceiverState, Writing, Reading),
    TRANSITION(TransceiverState, Reading, Sleeping)};

constexpr std::array readWriteTransitions{
    TRANSITION(TransceiverState, Initial, Sleeping),
    TRANSITION(TransceiverState, Sleeping, Reading),
    TRANSITION(TransceiverState, Reading, Writing),
    TRANSITION(TransceiverState, Writing, Sleeping)};

constexpr std::array messageStateTransitions{
    TRANSITION(MessageState, Idle, Writing),
    TRANSITION(MessageState, Writing, Reading),
    TRANSITION(MessageState, Reading, Idle)};

template <TransceiverMode Mode> class Transceiver {
    static constexpr auto Transitions = Mode == TransceiverMode::WriteRead
                                            ? writeReadTransitions
                                            : readWriteTransitions;
    using TransceiverMachine = StateMachine<TransceiverState, Transitions>;
    using MessageMachine = StateMachine<MessageState, messageStateTransitions>;

  public:
    explicit Transceiver(const char *ownerName)
        : _ownerName(ownerName), _state(_ownerName, TransceiverState::Initial),
          _messageState(_ownerName, MessageState::Idle) {}

    ReturnCode init(UartConfig uartConfig) {
        FAIL_IF_ERR_FWD(_uart.init(uartConfig),
                        "Failed to initialize UART for RS485 master");

        FAIL_IF_NOT_STATE(
            _state, TransceiverState::Initial, TransceiverState::Sleeping,
            "Failed to initialize transceiver for %s", _ownerName);

        return OK();
    }

    ReturnCode deinit() {
        FAIL_IF_ERR_FWD(_uart.deinit(),
                        "Failed to deinitialize UART for RS485");
        return OK();
    }

    [[nodiscard]] std::expected<TransceiverState, ReturnCode>
    nextCommandAction() const {
        return _state.nextState();
    }

    [[nodiscard]] std::expected<MessageState, ReturnCode>
    nextMessageAction() const {
        return _messageState.nextState();
    }

    ReturnCode done() {
        FAIL_IF_ERR_FWD(_messageState.transitionTo(MessageState::Idle),
                        "Failed to transition to sleep for %s", _ownerName);
        return OK();
    }

    ReturnCode send(WriteRequestHandle handle, const WriteRequest &request) {
        FAIL_IF_UNEXPECTED_FWD(
            header, Header::fromRequest(request),
            "Failed to create frame header from write request");
        _headerBuf = header.toBytes();
        _expectedResponseFrameType = header.expectedResponseType();

        FAIL_IF_ERR_FWD(_messageState.transitionTo(MessageState::Writing),
                        "Send failed");

        FAIL_IF_ERR_FWD(_uart.write(std::as_bytes(std::span(_headerBuf))),
                        "Failed to write frame header to UART for %s",
                        _ownerName);
        FAIL_IF_ERR_FWD(_uart.write(request.data, true /* drain */),
                        "Failed to write frame payload to UART for %s",
                        _ownerName);

        FAIL_IF_ERR_FWD(
            request.ack(handle, static_cast<uint16_t>(request.data.size())),
            "Failed to acknowledge write request for %s", _ownerName);

        if (!_expectedResponseFrameType) {
            FAIL_IF_ERR_FWD(_messageState.transitionTo(MessageState::Reading),
                            "Failed to skip read after write for %s",
                            _ownerName);
        }

        return OK();
    }

    ReturnCode read(ReadRequestHandle handle, ReadRequest &request) {
        FAIL_IF_ERR_FWD(_messageState.transitionTo(MessageState::Reading),
                        "Read failed for %s", _ownerName);

        FAIL_IF_UNEXPECTED_FWD(
            headerReadBytes,
            _uart.read(std::as_writable_bytes(std::span(_headerBuf))),
            "Failed to read frame header from UART for %s", _ownerName);
        FAIL_IF(headerReadBytes != Header::headerSize, ERR(Corrupted),
                "Incomplete frame header read from UART for %s", _ownerName);

        FAIL_IF_UNEXPECTED_FWD(header, Header::fromBytes(_headerBuf),
                               "Failed to parse frame header from UART for %s",
                               _ownerName);

        FAIL_IF(header.type != _expectedResponseFrameType.value_or(header.type),
                ERR(CoreError, InvalidState),
                "Received unexpected frame type 0x%02X when expecting 0x%02X "
                "in %s",
                static_cast<uint8_t>(header.type),
                static_cast<uint8_t>(
                    _expectedResponseFrameType.value_or(header.type)),
                _ownerName);

        FAIL_IF(header.payloadLength > request.data.size(),
                ERR(CoreError, InvalidState),
                "Frame payload length %u exceeds read buffer size %zu for %s",
                header.payloadLength, request.data.size(), _ownerName);

        FAIL_IF_UNEXPECTED_FWD(
            dataReadBytes, _uart.read(request.data.first(header.payloadLength)),
            "Failed to read frame payload from UART for %s", _ownerName);

        FAIL_IF(dataReadBytes != header.payloadLength, ERR(Corrupted),
                "Incomplete frame payload read from UART for %s", _ownerName);

        FAIL_IF_ERR_FWD(
            request.ack(handle, static_cast<uint16_t>(dataReadBytes)),
            "Failed to acknowledge read request for %s", _ownerName);

        return OK();
    }

  private:
    platform::Uart _uart;

    std::array<std::uint8_t, Header::headerSize> _headerBuf{};
    // std::optional<std::pair<WriteRequestHandle, WriteRequest>> _pendingWrite;
    std::optional<FrameType> _expectedResponseFrameType;

    const char *_ownerName;
    TransceiverMachine _state;
    MessageMachine _messageState;
};

} // namespace Totem::Wire::Rs485::detail
