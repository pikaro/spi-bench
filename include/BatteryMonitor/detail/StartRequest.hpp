// IWYU pragma: private

#pragma once

#include "BatteryMonitor/Interfaces/Config.hpp"
#include "BatteryMonitor/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include <cstdint>

namespace Totem::BatteryMonitor::detail {

[[nodiscard]] inline BatteryCalibrationStartResult
assessCalibrationStart(const BatteryStatus &status,
                       const BatteryConfig &config) {
    const auto reject = [](ReturnCode error,
                           BatteryCalibrationInvalidReason rejection) {
        return BatteryCalibrationStartResult{
            .error = error,
            .reason = rejection,
        };
    };

    switch (status.storageHealth) {
    case BatteryStorageHealth::Unavailable:
        return reject(ReturnCode::from(CoreError::InvalidState),
                      BatteryCalibrationInvalidReason::StorageUnavailable);
    case BatteryStorageHealth::Corrupt:
        return reject(ReturnCode::from(CoreError::InvalidState),
                      BatteryCalibrationInvalidReason::CorruptJournal);
    case BatteryStorageHealth::WriteFailed:
        return reject(ReturnCode::from(CoreError::InvalidState),
                      BatteryCalibrationInvalidReason::StorageError);
    case BatteryStorageHealth::Full:
        return reject(ReturnCode::from(CoreError::Overflow),
                      BatteryCalibrationInvalidReason::StorageUnavailable);
    case BatteryStorageHealth::Healthy:
        break;
    }

    if (status.calibrationState == BatteryCalibrationState::ArmedFull ||
        status.calibrationState == BatteryCalibrationState::Discharging ||
        status.calibrationState == BatteryCalibrationState::Finalizing) {
        return reject(ReturnCode::from(CoreError::InvalidState),
                      BatteryCalibrationInvalidReason::None);
    }
    if (!status.latestMeasurement.has_value()) {
        return reject(ReturnCode::from(CoreError::NotFinished),
                      BatteryCalibrationInvalidReason::SensorTimeout);
    }
    if (status.measurementFreshness != BatteryMeasurementFreshness::Fresh) {
        return reject(ReturnCode::from(CoreError::NotFinished),
                      BatteryCalibrationInvalidReason::SensorTimeout);
    }

    const auto &measurement = *status.latestMeasurement;
    if (measurement.voltageMillivolts <
        config.fullQualificationMinPackMillivolts()) {
        return reject(ReturnCode::from(CoreError::InvalidState),
                      BatteryCalibrationInvalidReason::NotFull);
    }
    if (measurement.voltageMillivolts > config.absoluteMaxPackMillivolts()) {
        return reject(ReturnCode::from(CoreError::InvalidState),
                      BatteryCalibrationInvalidReason::AbsoluteLimit);
    }
    if (measurement.currentMicroamps <
        -static_cast<int32_t>(config.currentDeadbandMicroamps)) {
        return reject(ReturnCode::from(CoreError::InvalidState),
                      BatteryCalibrationInvalidReason::ChargingDetected);
    }
    if (measurement.currentMicroamps <
        static_cast<int32_t>(config.minimumDischargeMicroamps)) {
        return reject(ReturnCode::from(CoreError::InvalidState),
                      BatteryCalibrationInvalidReason::NoDischargeLoad);
    }
    return {};
}

} // namespace Totem::BatteryMonitor::detail
