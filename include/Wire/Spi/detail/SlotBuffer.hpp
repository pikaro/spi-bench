#pragma once

#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include "Wire/Spi/detail/Pdu.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>

namespace Totem::Wire::Spi::detail {

template <size_t Capacity> class SlotBuffer {
  public:
    static_assert(Capacity >= bucketBytes(BucketSize::B64),
                  "SPI slot buffer must fit at least the smallest bucket");

    void reset(uint8_t peerId, uint8_t connectionId, uint16_t sequence,
               BucketSize bucket, SlotFlags flags = SlotFlags::None,
               uint16_t ackSequence = 0) {
        _header = {
            .slotLength = static_cast<uint16_t>(SlotHeader::size),
            .bucketLength = bucketBytes(bucket),
            .peerId = peerId,
            .connectionId = connectionId,
            .sequence = sequence,
            .ackSequence = ackSequence,
            .flags = flags,
            .frameCount = 0,
            .payloadBytes = 0,
            .crc = 0,
        };
        _writeOffset = SlotHeader::size;
    }

    void clear(SlotFlags flags = SlotFlags::None) {
        reset(_header.peerId, _header.connectionId, _header.sequence,
              static_cast<BucketSize>(_header.bucketLength), flags,
              _header.ackSequence);
    }

    ReturnCode appendFrame(FrameType type, PayloadType payloadType,
                           std::span<const std::byte> payload,
                           uint16_t sequence = 0, uint16_t responseTo = 0,
                           FrameFlags flags = FrameFlags::None,
                           size_t *payloadOffset = nullptr) {
        FAIL_IF(payload.size() > std::numeric_limits<uint16_t>::max(),
                ERR(CoreError, Overflow), "SPI frame payload too large");
        const auto required = FrameHeader::size + payload.size();
        FAIL_IF(_writeOffset + required > _header.bucketLength,
                ERR(CoreError, Overflow), "SPI slot bucket full");
        FAIL_IF(_writeOffset + required > _bytes.size(),
                ERR(CoreError, Overflow), "SPI slot buffer full");

        FrameHeader frame{
            .type = type,
            .payloadType = payloadType,
            .sequence = sequence,
            .responseTo = responseTo,
            .payloadLength = static_cast<uint16_t>(payload.size()),
            .flags = flags,
            .crc = 0,
        };
        frame.refreshCrc();
        auto frameBytes = frame.toBytes();
        std::copy(frameBytes.begin(), frameBytes.end(),
                  _bytes.begin() + _writeOffset);
        _writeOffset += frameBytes.size();
        if (payloadOffset != nullptr) {
            *payloadOffset = _writeOffset;
        }
        std::copy(payload.begin(), payload.end(), _bytes.begin() + _writeOffset);
        _writeOffset += payload.size();

        _header.frameCount++;
        _header.payloadBytes =
            static_cast<uint16_t>(_writeOffset - SlotHeader::size);
        _header.slotLength = static_cast<uint16_t>(_writeOffset);
        return OK();
    }

    std::span<const std::byte> finalize() {
        _header.refreshCrc();
        auto headerBytes = _header.toBytes();
        std::copy(headerBytes.begin(), headerBytes.end(), _bytes.begin());
        return std::span<const std::byte>(_bytes.data(), _header.bucketLength);
    }

    [[nodiscard]] const SlotHeader &header() const { return _header; }
    [[nodiscard]] uint8_t frameCount() const { return _header.frameCount; }
    void addFlags(SlotFlags flags) { _header.flags = _header.flags | flags; }
    void setAckSequence(uint16_t ackSequence) {
        _header.ackSequence = ackSequence;
    }
    ReturnCode ensureBucketFor(size_t bytes) {
        const auto bucketLength = bucketBytes(bucketFor(bytes));
        FAIL_IF(bucketLength > _bytes.size(), ERR(CoreError, Overflow),
                "SPI slot buffer cannot fit requested bucket");
        if (bucketLength > _header.bucketLength) {
            _header.bucketLength = bucketLength;
        }
        return OK();
    }
    [[nodiscard]] std::span<std::byte> writableBucket(BucketSize bucket) {
        return std::span<std::byte>(_bytes.data(), bucketBytes(bucket));
    }
    [[nodiscard]] std::span<std::byte> writableSpan(size_t offset,
                                                    size_t length) {
        return std::span<std::byte>(_bytes.data() + offset, length);
    }
    [[nodiscard]] std::span<const std::byte> bytes() const {
        return std::span<const std::byte>(_bytes.data(), _header.bucketLength);
    }
    [[nodiscard]] const void *data() const { return _bytes.data(); }

  private:
    alignas(4) std::array<std::byte, Capacity> _bytes{};
    SlotHeader _header{};
    size_t _writeOffset = SlotHeader::size;
};

class SlotReader {
  public:
    static std::expected<SlotReader, ReturnCode>
    parse(std::span<const std::byte> bytes) {
        auto headerResult = SlotHeader::fromBytes(bytes);
        if (!headerResult) {
            return std::unexpected(headerResult.error());
        }
        const auto header = *headerResult;
        if (bytes.size() < header.slotLength) {
            return std::unexpected(ERR(WireError, Corrupted));
        }
        return SlotReader{bytes.first(header.slotLength), header};
    }

    [[nodiscard]] const SlotHeader &header() const { return _header; }
    [[nodiscard]] uint8_t framesRead() const { return _framesRead; }

    std::expected<FrameView, ReturnCode> next() {
        if (_framesRead >= _header.frameCount) {
            return std::unexpected(ERR(CoreError, NotFound));
        }
        if (_offset + FrameHeader::size > _bytes.size()) {
            return std::unexpected(ERR(WireError, Corrupted));
        }
        auto frameResult =
            FrameHeader::fromBytes(_bytes.subspan(_offset, FrameHeader::size));
        if (!frameResult) {
            return std::unexpected(frameResult.error());
        }
        _offset += FrameHeader::size;
        auto frame = *frameResult;
        if (_offset + frame.payloadLength > _bytes.size()) {
            return std::unexpected(ERR(WireError, Corrupted));
        }
        auto payload = _bytes.subspan(_offset, frame.payloadLength);
        _offset += frame.payloadLength;
        _framesRead++;
        return FrameView{
            .header = frame,
            .payload = payload,
        };
    }

  private:
    SlotReader(std::span<const std::byte> bytes, SlotHeader header)
        : _bytes(bytes), _header(header) {}

    std::span<const std::byte> _bytes{};
    SlotHeader _header{};
    size_t _offset = SlotHeader::size;
    uint8_t _framesRead = 0;
};

} // namespace Totem::Wire::Spi::detail
