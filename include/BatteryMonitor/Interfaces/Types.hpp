#pragma once

#include "BatteryMonitor/Interfaces/Measurement.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace Totem::BatteryMonitor {

enum class BatteryChemistry : uint8_t {
    LiIon4V2,
};

enum class BatterySourceState : uint8_t {
    Unknown,
    Absent,
    AbsoluteUnder,
    PracticalUnder,
    Normal,
    PracticalOver,
    AbsoluteOver,
};

enum class BatteryEstimateConfidence : uint8_t {
    Unavailable,
    VoltageOnly,
    IntegratedNominal,
    IntegratedLearned,
};

/** Describes whether the published measurement may be used at `nowMs`. */
enum class BatteryMeasurementFreshness : uint8_t {
    NeverReceived,
    Fresh,
    Stale,
};

/** Persistent calibration storage state. Runtime estimation remains available.
 */
enum class BatteryStorageHealth : uint8_t {
    Unavailable,
    Healthy,
    Corrupt,
    WriteFailed,
    Full,
};

enum class BatteryCalibrationState : uint8_t {
    Idle,
    ArmedFull,
    Discharging,
    Finalizing,
    Complete,
    Invalid,
    Aborted,
};

enum class BatteryCalibrationInvalidReason : uint8_t {
    None,
    NotFull,
    NoDischargeLoad,
    SampleGap,
    SensorTimeout,
    ChargingDetected,
    LoadRemoved,
    AbsoluteLimit,
    StorageUnavailable,
    StorageError,
    Rebooted,
    CorruptJournal,
    DurationExceeded,
    UserAbort,
};

/** Queue outcome; `reason` identifies a calibration-specific preflight failure.
 */
struct BatteryCalibrationStartResult {
    ReturnCode error = ReturnCode::from(CoreError::Ok);
    BatteryCalibrationInvalidReason reason =
        BatteryCalibrationInvalidReason::None;

    [[nodiscard]] bool queued() const { return error.ok(); }
};

struct BatteryProfileStatus {
    uint32_t sessionId = 0;
    uint32_t usableMilliampHours = 0;
    uint32_t usableMilliwattHours = 0;
    uint32_t durationSeconds = 0;
    uint32_t observedCutoffMillivolts = 0;
    uint16_t pointCount = 0;
    bool active = false;
};

inline constexpr std::size_t batteryProfilePointCount = 101;

struct BatteryProfilePoint {
    uint32_t loadedVoltageMillivolts = 0;
    int32_t representativeCurrentMicroamps = 0;
};

struct BatteryProfile {
    BatteryProfileStatus status{};
    std::array<BatteryProfilePoint, batteryProfilePointCount> points{};
};

/** Bounded journal summary published without exposing journal internals. */
struct BatteryProfileCatalog {
    uint32_t highestSessionId = 0;
    uint32_t completeSessionCount = 0;
    uint32_t incompatibleSessionCount = 0;
    uint32_t incompleteSessionCount = 0;
    uint32_t corruptRecordCount = 0;
    uint32_t journalRecordCount = 0;
};

/** Successful processing outcome for one sensor measurement. */
struct BatteryObservation {
    BatterySourceState sourceState = BatterySourceState::Unknown;
    BatteryCalibrationState calibrationState = BatteryCalibrationState::Idle;
    BatteryCalibrationInvalidReason calibrationReason =
        BatteryCalibrationInvalidReason::None;
    uint32_t elapsedMs = 0;
    bool sampleGapExceeded = false;
    bool chargingDetected = false;
    bool integrated = false;
};

struct BatteryStatus {
    std::optional<BatteryMeasurement> latestMeasurement{};
    std::optional<uint32_t> timeToEmptyMinutes{};
    BatteryProfileStatus profile{};
    BatteryProfileCatalog catalog{};
    uint32_t stateOfChargePartsPerThousand = 0;
    uint32_t remainingMilliampHours = 0;
    uint32_t remainingMilliwattHours = 0;
    uint32_t dischargedMilliampHours = 0;
    uint32_t dischargedMilliwattHours = 0;
    uint32_t averagePowerMilliwatts = 0;
    uint32_t sampleCount = 0;
    uint32_t maximumSampleGapMs = 0;
    uint32_t persistedCalibrationIntervals = 0;
    BatterySourceState sourceState = BatterySourceState::Unknown;
    BatteryMeasurementFreshness measurementFreshness =
        BatteryMeasurementFreshness::NeverReceived;
    BatteryStorageHealth storageHealth = BatteryStorageHealth::Unavailable;
    BatteryEstimateConfidence confidence =
        BatteryEstimateConfidence::Unavailable;
    BatteryCalibrationState calibrationState = BatteryCalibrationState::Idle;
    BatteryCalibrationInvalidReason calibrationReason =
        BatteryCalibrationInvalidReason::None;
};

static_assert(std::is_trivially_copyable_v<BatteryObservation>);
static_assert(std::is_trivially_copyable_v<BatteryStatus>);
static_assert(std::is_trivially_copyable_v<BatteryCalibrationStartResult>);

} // namespace Totem::BatteryMonitor
