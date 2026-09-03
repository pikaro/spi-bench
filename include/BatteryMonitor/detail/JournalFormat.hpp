// IWYU pragma: private

#pragma once

#include "BatteryMonitor/Interfaces/Config.hpp"
#include "BatteryMonitor/Interfaces/Types.hpp"
#include "Platform/Crc/Facade.hpp"
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Totem::BatteryMonitor::detail {

inline constexpr size_t journalRecordSize = 64;
inline constexpr size_t journalPayloadOffset = 16;
inline constexpr size_t journalPayloadSize = 44;
inline constexpr size_t journalCrcOffset = 60;
inline constexpr uint32_t journalMagic = 0x4E4F4D42U; // "BMON" little-endian
inline constexpr uint8_t journalVersion = 2;
inline constexpr uint16_t profilePointCount = batteryProfilePointCount;

enum class JournalRecordType : uint8_t {
    SessionHeader = 1,
    Interval = 2,
    ProfilePoint = 3,
    SessionFooter = 4,
};

using JournalRecord = std::array<std::byte, journalRecordSize>;
using JournalPayload = std::array<std::byte, journalPayloadSize>;

struct DecodedJournalRecord {
    JournalRecordType type = JournalRecordType::SessionHeader;
    uint32_t sequence = 0;
    uint32_t sessionId = 0;
    std::span<const std::byte> payload{};
    uint32_t crc = 0;
};

struct IntervalRecordData {
    uint32_t elapsedSeconds = 0;
    uint32_t averageVoltageMillivolts = 0;
    uint32_t minimumVoltageMillivolts = 0;
    uint32_t maximumVoltageMillivolts = 0;
    int32_t averageCurrentMicroamps = 0;
    int32_t averagePowerMilliwatts = 0;
    uint32_t cumulativeMilliampHours = 0;
    uint32_t cumulativeMilliwattHours = 0;
    uint32_t sampleCount = 0;
    uint32_t maximumGapMs = 0;
    uint32_t lastUnderLoadMillivolts = 0;
};

struct ProfilePointRecordData {
    uint16_t stateOfChargePercent = 0;
    uint32_t loadedVoltageMillivolts = 0;
    int32_t representativeCurrentMicroamps = 0;
};

struct FooterRecordData {
    BatteryCalibrationState state = BatteryCalibrationState::Invalid;
    BatteryCalibrationInvalidReason reason =
        BatteryCalibrationInvalidReason::None;
    uint16_t pointCount = 0;
    uint32_t usableMilliampHours = 0;
    uint32_t usableMilliwattHours = 0;
    uint32_t durationSeconds = 0;
    uint32_t observedCutoffMillivolts = 0;
    uint32_t lastUnderLoadMillivolts = 0;
    uint32_t intervalCount = 0;
    uint32_t maximumGapMs = 0;
    uint32_t recordsChecksum = 0;
};

inline constexpr void putU8(std::span<std::byte> output, size_t offset,
                            uint8_t value) {
    output[offset] = static_cast<std::byte>(value);
}

inline constexpr void putU16(std::span<std::byte> output, size_t offset,
                             uint16_t value) {
    putU8(output, offset, static_cast<uint8_t>(value));
    putU8(output, offset + 1U, static_cast<uint8_t>(value >> 8U));
}

inline constexpr void putU32(std::span<std::byte> output, size_t offset,
                             uint32_t value) {
    putU8(output, offset, static_cast<uint8_t>(value));
    putU8(output, offset + 1U, static_cast<uint8_t>(value >> 8U));
    putU8(output, offset + 2U, static_cast<uint8_t>(value >> 16U));
    putU8(output, offset + 3U, static_cast<uint8_t>(value >> 24U));
}

[[nodiscard]] inline constexpr uint8_t getU8(std::span<const std::byte> input,
                                             size_t offset) {
    return std::to_integer<uint8_t>(input[offset]);
}

[[nodiscard]] inline constexpr uint16_t getU16(std::span<const std::byte> input,
                                               size_t offset) {
    return static_cast<uint16_t>(getU8(input, offset)) |
           static_cast<uint16_t>(
               static_cast<uint16_t>(getU8(input, offset + 1U)) << 8U);
}

[[nodiscard]] inline constexpr uint32_t getU32(std::span<const std::byte> input,
                                               size_t offset) {
    return static_cast<uint32_t>(getU8(input, offset)) |
           (static_cast<uint32_t>(getU8(input, offset + 1U)) << 8U) |
           (static_cast<uint32_t>(getU8(input, offset + 2U)) << 16U) |
           (static_cast<uint32_t>(getU8(input, offset + 3U)) << 24U);
}

[[nodiscard]] inline uint32_t configHash(const BatteryConfig &config) {
    std::array<std::byte, 52> data{};
    putU8(data, 0, static_cast<uint8_t>(config.chemistry));
    putU8(data, 1, config.seriesCells);
    putU8(data, 2, config.parallelCells);
    putU32(data, 4, config.packId);
    putU32(data, 8, config.nominalPackCapacityMilliampHours);
    putU32(data, 12, config.nominalCellMillivolts);
    putU32(data, 16, config.practicalMinCellMillivolts);
    putU32(data, 20, config.practicalMaxCellMillivolts);
    putU32(data, 24, config.absoluteMinCellMillivolts);
    putU32(data, 28, config.absoluteMaxCellMillivolts);
    putU32(data, 32, config.sampleGapToleranceMs);
    putU32(data, 36, config.persistenceIntervalMs);
    putU32(data, 40, config.currentDeadbandMicroamps);
    putU32(data, 44, config.calibrationLoadMilliohms);
    putU32(data, 48, config.disconnectedVoltageMillivolts);
    return Platform::Crc::Platform::crc32(data);
}

[[nodiscard]] inline JournalPayload
makeHeaderPayload(const BatteryConfig &config, uint32_t startedAtMs) {
    JournalPayload payload{};
    putU32(payload, 0, configHash(config));
    putU32(payload, 4, startedAtMs);
    putU32(payload, 8, config.packId);
    putU32(payload, 12, config.nominalPackCapacityMilliampHours);
    putU32(payload, 16, config.nominalPackEnergyMilliwattHours());
    putU32(payload, 20, config.calibrationLoadMilliohms);
    putU32(payload, 24, config.practicalMinPackMillivolts());
    putU32(payload, 28, config.practicalMaxPackMillivolts());
    putU8(payload, 32, config.seriesCells);
    putU8(payload, 33, config.parallelCells);
    putU8(payload, 34, static_cast<uint8_t>(config.chemistry));
    return payload;
}

[[nodiscard]] inline JournalPayload
makeIntervalPayload(const IntervalRecordData &data) {
    JournalPayload payload{};
    putU32(payload, 0, data.elapsedSeconds);
    putU32(payload, 4, data.averageVoltageMillivolts);
    putU32(payload, 8, data.minimumVoltageMillivolts);
    putU32(payload, 12, data.maximumVoltageMillivolts);
    putU32(payload, 16, std::bit_cast<uint32_t>(data.averageCurrentMicroamps));
    putU32(payload, 20, std::bit_cast<uint32_t>(data.averagePowerMilliwatts));
    putU32(payload, 24, data.cumulativeMilliampHours);
    putU32(payload, 28, data.cumulativeMilliwattHours);
    putU32(payload, 32, data.sampleCount);
    putU32(payload, 36, data.maximumGapMs);
    putU32(payload, 40, data.lastUnderLoadMillivolts);
    return payload;
}

[[nodiscard]] inline IntervalRecordData
decodeIntervalPayload(std::span<const std::byte> payload) {
    return {
        .elapsedSeconds = getU32(payload, 0),
        .averageVoltageMillivolts = getU32(payload, 4),
        .minimumVoltageMillivolts = getU32(payload, 8),
        .maximumVoltageMillivolts = getU32(payload, 12),
        .averageCurrentMicroamps = std::bit_cast<int32_t>(getU32(payload, 16)),
        .averagePowerMilliwatts = std::bit_cast<int32_t>(getU32(payload, 20)),
        .cumulativeMilliampHours = getU32(payload, 24),
        .cumulativeMilliwattHours = getU32(payload, 28),
        .sampleCount = getU32(payload, 32),
        .maximumGapMs = getU32(payload, 36),
        .lastUnderLoadMillivolts = getU32(payload, 40),
    };
}

[[nodiscard]] inline JournalPayload
makeProfilePointPayload(const ProfilePointRecordData &data) {
    JournalPayload payload{};
    putU16(payload, 0, data.stateOfChargePercent);
    putU32(payload, 4, data.loadedVoltageMillivolts);
    putU32(payload, 8,
           std::bit_cast<uint32_t>(data.representativeCurrentMicroamps));
    return payload;
}

[[nodiscard]] inline ProfilePointRecordData
decodeProfilePointPayload(std::span<const std::byte> payload) {
    return {
        .stateOfChargePercent = getU16(payload, 0),
        .loadedVoltageMillivolts = getU32(payload, 4),
        .representativeCurrentMicroamps =
            std::bit_cast<int32_t>(getU32(payload, 8)),
    };
}

[[nodiscard]] inline JournalPayload
makeFooterPayload(const FooterRecordData &data) {
    JournalPayload payload{};
    putU8(payload, 0, static_cast<uint8_t>(data.state));
    putU8(payload, 1, static_cast<uint8_t>(data.reason));
    putU16(payload, 2, data.pointCount);
    putU32(payload, 4, data.usableMilliampHours);
    putU32(payload, 8, data.usableMilliwattHours);
    putU32(payload, 12, data.durationSeconds);
    putU32(payload, 16, data.observedCutoffMillivolts);
    putU32(payload, 20, data.lastUnderLoadMillivolts);
    putU32(payload, 24, data.intervalCount);
    putU32(payload, 28, data.maximumGapMs);
    putU32(payload, 32, data.recordsChecksum);
    return payload;
}

[[nodiscard]] inline FooterRecordData
decodeFooterPayload(std::span<const std::byte> payload) {
    return {
        .state = static_cast<BatteryCalibrationState>(getU8(payload, 0)),
        .reason =
            static_cast<BatteryCalibrationInvalidReason>(getU8(payload, 1)),
        .pointCount = getU16(payload, 2),
        .usableMilliampHours = getU32(payload, 4),
        .usableMilliwattHours = getU32(payload, 8),
        .durationSeconds = getU32(payload, 12),
        .observedCutoffMillivolts = getU32(payload, 16),
        .lastUnderLoadMillivolts = getU32(payload, 20),
        .intervalCount = getU32(payload, 24),
        .maximumGapMs = getU32(payload, 28),
        .recordsChecksum = getU32(payload, 32),
    };
}

[[nodiscard]] inline JournalRecord
makeJournalRecord(JournalRecordType type, uint32_t sequence, uint32_t sessionId,
                  const JournalPayload &payload) {
    JournalRecord record{};
    putU32(record, 0, journalMagic);
    putU8(record, 4, journalVersion);
    putU8(record, 5, static_cast<uint8_t>(type));
    putU16(record, 6, journalPayloadSize);
    putU32(record, 8, sequence);
    putU32(record, 12, sessionId);
    for (size_t i = 0; i < payload.size(); ++i) {
        record[journalPayloadOffset + i] = payload[i];
    }
    putU32(record, journalCrcOffset,
           Platform::Crc::Platform::crc32(
               std::span<const std::byte>{record}.first(journalCrcOffset)));
    return record;
}

[[nodiscard]] inline bool decodeJournalRecord(const JournalRecord &record,
                                              DecodedJournalRecord &decoded) {
    if (getU32(record, 0) != journalMagic ||
        getU8(record, 4) != journalVersion ||
        getU16(record, 6) != journalPayloadSize) {
        return false;
    }
    const auto rawType = getU8(record, 5);
    if (rawType < static_cast<uint8_t>(JournalRecordType::SessionHeader) ||
        rawType > static_cast<uint8_t>(JournalRecordType::SessionFooter)) {
        return false;
    }
    const auto expectedCrc = getU32(record, journalCrcOffset);
    if (expectedCrc !=
        Platform::Crc::Platform::crc32(
            std::span<const std::byte>{record}.first(journalCrcOffset))) {
        return false;
    }
    decoded = {
        .type = static_cast<JournalRecordType>(rawType),
        .sequence = getU32(record, 8),
        .sessionId = getU32(record, 12),
        .payload =
            std::span<const std::byte, journalPayloadSize>{
                record.data() + journalPayloadOffset, journalPayloadSize},
        .crc = expectedCrc,
    };
    return true;
}

static_assert(journalPayloadOffset + journalPayloadSize == journalCrcOffset);

} // namespace Totem::BatteryMonitor::detail
