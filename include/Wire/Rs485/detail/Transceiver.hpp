#pragma once

#include "Macros/Facade.hpp"
#include "Platform/platform/PlatformESP32/Uart.hpp"
#include "Types/Error.hpp"
#include "Types/Uart.hpp"
#include "Wire/Interfaces/Request.hpp"
#include "Wire/Rs485/detail/Pdu.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace Totem::Wire::Rs485::detail {

class Transceiver {
  public:
    explicit Transceiver(const char *ownerName) : _ownerName(ownerName) {}

    ReturnCode init(UartConfig uartConfig) {
        FAIL_IF_ERR_FWD(_uart.init(uartConfig),
                        "Failed to initialize UART for RS485 %s", _ownerName);
        _uartConfig = uartConfig;
        return OK();
    }

    ReturnCode deinit() {
        FAIL_IF_ERR_FWD(_uart.deinit(),
                        "Failed to deinitialize UART for RS485 %s", _ownerName);
        return OK();
    }

    ReturnCode sendFrame(FrameType type, PayloadType payloadType,
                         std::span<const std::byte> payload,
                         uint8_t responseTo = 0, Header *sentHeader = nullptr) {
        FAIL_IF(payload.size() > UINT16_MAX, ERR(CoreError, InvalidArgument),
                "RS485 payload too large for %s", _ownerName);
        auto header =
            Header::make(type, payloadType,
                         static_cast<uint16_t>(payload.size()), responseTo);
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
                           Header *sentHeader = nullptr) {
        return sendFrame(type, payloadType, {}, responseTo, sentHeader);
    }

    std::expected<Header, ReturnCode> pollHeader() {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            availableBytes, _uart.available(),
            "Failed to check RS485 header availability for %s", _ownerName);
        if (availableBytes < Header::headerSize) {
            return std::unexpected(ERR(CoreError, NotFound));
        }
        return receiveHeader();
    }

    std::expected<Header, ReturnCode> receiveHeader(uint32_t timeoutMs = 0) {
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
        return Header::fromBytes(_headerBuf);
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

  private:
    ReturnCode writeHeader(const Header &header) {
        _headerBuf = header.toBytes();
        return _uart.write(std::as_bytes(std::span(_headerBuf)));
    }

    platform::Uart _uart;
    UartConfig _uartConfig{};
    std::array<std::uint8_t, Header::headerSize> _headerBuf{};
    const char *_ownerName;
};

} // namespace Totem::Wire::Rs485::detail
