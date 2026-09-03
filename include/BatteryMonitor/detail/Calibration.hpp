// IWYU pragma: private

#pragma once

#include "BatteryMonitor/Interfaces/Config.hpp"
#include "BatteryMonitor/Interfaces/Measurement.hpp"
#include "BatteryMonitor/Interfaces/Types.hpp"
#include "BatteryMonitor/detail/Arithmetic.hpp"
#include "BatteryMonitor/detail/Estimator.hpp"
#include "BatteryMonitor/detail/JournalFormat.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace Totem::BatteryMonitor::detail {

struct CalibrationObservation {
    bool started = false;
    bool enteredFinalizing = false;
    bool invalidated = false;
};

class Calibration {
  public:
    void begin(const BatteryConfig &config) {
        _config = &config;
        resetRuntime();
    }

    [[nodiscard]] ReturnCode arm(const BatteryStatus &status,
                                 uint32_t sessionId) {
        if (active()) {
            return ReturnCode::from(CoreError::InvalidState);
        }
        if (status.measurementFreshness != BatteryMeasurementFreshness::Fresh) {
            _state = BatteryCalibrationState::Invalid;
            _reason = BatteryCalibrationInvalidReason::SensorTimeout;
            return ReturnCode::from(CoreError::InvalidState);
        }
        if (!status.latestMeasurement.has_value() ||
            status.latestMeasurement->voltageMillivolts <
                _config->fullQualificationMinPackMillivolts() ||
            status.latestMeasurement->voltageMillivolts >
                _config->absoluteMaxPackMillivolts()) {
            _state = BatteryCalibrationState::Invalid;
            _reason = BatteryCalibrationInvalidReason::NotFull;
            return ReturnCode::from(CoreError::InvalidState);
        }
        if (status.latestMeasurement->currentMicroamps <
            static_cast<int32_t>(_config->minimumDischargeMicroamps)) {
            _state = BatteryCalibrationState::Invalid;
            _reason = BatteryCalibrationInvalidReason::NoDischargeLoad;
            return ReturnCode::from(CoreError::InvalidState);
        }

        resetRuntime();
        _sessionId = sessionId;
        _state = BatteryCalibrationState::ArmedFull;
        return ReturnCode::from(CoreError::Ok);
    }

    [[nodiscard]] CalibrationObservation
    observe(const BatteryMeasurement &measurement,
            const EstimatorObservation &estimatorObservation,
            const BatteryStatus &estimatorStatus) {
        CalibrationObservation result{};
        if (_state == BatteryCalibrationState::ArmedFull) {
            if (measurement.voltageMillivolts <
                    _config->fullQualificationMinPackMillivolts() ||
                measurement.voltageMillivolts >
                    _config->absoluteMaxPackMillivolts()) {
                invalidate(BatteryCalibrationInvalidReason::NotFull);
                result.invalidated = true;
                return result;
            }
            if (measurement.currentMicroamps <
                static_cast<int32_t>(_config->minimumDischargeMicroamps)) {
                invalidate(BatteryCalibrationInvalidReason::NoDischargeLoad);
                result.invalidated = true;
                return result;
            }

            _state = BatteryCalibrationState::Discharging;
            _reason = BatteryCalibrationInvalidReason::None;
            _startedAtMs = measurement.capturedAtMs;
            _started = true;
            _lastMeasurementAtMs = measurement.capturedAtMs;
            _intervalStartedAtMs = measurement.capturedAtMs;
            _lastUnderLoadMillivolts = measurement.voltageMillivolts;
            _headerPending = true;
            addToInterval(measurement, 0);
            result.started = true;
            return result;
        }

        if (_state != BatteryCalibrationState::Discharging) {
            return result;
        }
        _lastMeasurementAtMs = measurement.capturedAtMs;

        if (elapsedMs(measurement.capturedAtMs, _startedAtMs) >
            _config->maximumCalibrationDurationMs) {
            queuePartialInterval(estimatorStatus);
            invalidate(BatteryCalibrationInvalidReason::DurationExceeded);
            result.invalidated = true;
            return result;
        }

        if (estimatorObservation.sampleGapExceeded) {
            queuePartialInterval(estimatorStatus);
            invalidate(BatteryCalibrationInvalidReason::SampleGap);
            result.invalidated = true;
            return result;
        }
        if (estimatorObservation.chargingDetected) {
            queuePartialInterval(estimatorStatus);
            invalidate(BatteryCalibrationInvalidReason::ChargingDetected);
            result.invalidated = true;
            return result;
        }
        if (estimatorStatus.sourceState == BatterySourceState::AbsoluteUnder ||
            estimatorStatus.sourceState == BatterySourceState::AbsoluteOver) {
            queuePartialInterval(estimatorStatus);
            invalidate(BatteryCalibrationInvalidReason::AbsoluteLimit);
            result.invalidated = true;
            return result;
        }

        const bool disconnected =
            measurement.voltageMillivolts <=
                _config->disconnectedVoltageMillivolts &&
            absoluteCurrent(measurement.currentMicroamps) <=
                _config->cutoffCurrentMicroamps;
        if (disconnected) {
            if (!_cutoffCandidateAtMs.has_value()) {
                _cutoffCandidateAtMs = measurement.capturedAtMs;
                _observedCutoffMillivolts = measurement.voltageMillivolts;
            }
            if (elapsedMs(measurement.capturedAtMs, *_cutoffCandidateAtMs) >=
                _config->cutoffDwellMs) {
                queuePartialInterval(estimatorStatus);
                _state = BatteryCalibrationState::Finalizing;
                result.enteredFinalizing = true;
            }
            return result;
        }
        _cutoffCandidateAtMs.reset();

        const bool loadMissing =
            absoluteCurrent(measurement.currentMicroamps) <=
            _config->currentDeadbandMicroamps;
        if (loadMissing) {
            if (!_loadRemovedCandidateAtMs.has_value()) {
                _loadRemovedCandidateAtMs = measurement.capturedAtMs;
            }
            if (elapsedMs(measurement.capturedAtMs,
                          *_loadRemovedCandidateAtMs) >=
                _config->loadRemovedDwellMs) {
                queuePartialInterval(estimatorStatus);
                invalidate(BatteryCalibrationInvalidReason::LoadRemoved);
                result.invalidated = true;
            }
            return result;
        }
        _loadRemovedCandidateAtMs.reset();

        _lastUnderLoadMillivolts = measurement.voltageMillivolts;
        addToInterval(measurement, estimatorObservation.elapsedMs);
        if (elapsedMs(measurement.capturedAtMs, _intervalStartedAtMs) >=
            _config->persistenceIntervalMs) {
            queueInterval(estimatorStatus);
        }
        return result;
    }

    void checkSensorTimeout(uint32_t nowMs,
                            const BatteryStatus &estimatorStatus) {
        if (_state == BatteryCalibrationState::Discharging &&
            elapsedMs(nowMs, _lastMeasurementAtMs) >
                _config->sampleGapToleranceMs) {
            queuePartialInterval(estimatorStatus);
            invalidate(BatteryCalibrationInvalidReason::SensorTimeout);
        }
    }

    [[nodiscard]] ReturnCode abort(const BatteryStatus &estimatorStatus) {
        if (!active()) {
            return ReturnCode::from(CoreError::InvalidState);
        }
        if (_state == BatteryCalibrationState::Discharging) {
            queuePartialInterval(estimatorStatus);
        }
        _state = BatteryCalibrationState::Aborted;
        _reason = BatteryCalibrationInvalidReason::UserAbort;
        _footerPending = _headerWritten;
        return ReturnCode::from(CoreError::Ok);
    }

    void rejectStart(BatteryCalibrationInvalidReason reason) {
        if (!active()) {
            _state = BatteryCalibrationState::Invalid;
            _reason = reason;
        }
    }

    void markHeaderWritten() {
        _headerPending = false;
        _headerWritten = true;
    }

    void markIntervalWritten() {
        _pendingInterval.reset();
        ++_persistedIntervals;
    }

    void markComplete() {
        _state = BatteryCalibrationState::Complete;
        _reason = BatteryCalibrationInvalidReason::None;
        _footerPending = false;
    }

    void markFooterWritten() { _footerPending = false; }

    void storageFailed() {
        _state = BatteryCalibrationState::Invalid;
        _reason = BatteryCalibrationInvalidReason::StorageError;
        _headerPending = false;
        _footerPending = false;
        _pendingInterval.reset();
        _headerWritten = false;
    }

    [[nodiscard]] bool active() const {
        return _state == BatteryCalibrationState::ArmedFull ||
               _state == BatteryCalibrationState::Discharging ||
               _state == BatteryCalibrationState::Finalizing;
    }

    [[nodiscard]] bool headerPending() const { return _headerPending; }
    [[nodiscard]] bool headerWritten() const { return _headerWritten; }
    [[nodiscard]] bool footerPending() const { return _footerPending; }
    [[nodiscard]] bool intervalPending() const {
        return _pendingInterval.has_value();
    }
    [[nodiscard]] const IntervalRecordData &pendingInterval() const {
        return *_pendingInterval;
    }
    [[nodiscard]] BatteryCalibrationState state() const { return _state; }
    [[nodiscard]] BatteryCalibrationInvalidReason reason() const {
        return _reason;
    }
    [[nodiscard]] uint32_t sessionId() const { return _sessionId; }
    [[nodiscard]] uint32_t startedAtMs() const { return _startedAtMs; }
    [[nodiscard]] uint32_t durationMs(uint32_t nowMs) const {
        return _started ? elapsedMs(nowMs, _startedAtMs) : 0;
    }
    [[nodiscard]] uint32_t lastMeasurementAtMs() const {
        return _lastMeasurementAtMs;
    }
    [[nodiscard]] uint32_t observedCutoffMillivolts() const {
        return _observedCutoffMillivolts;
    }
    [[nodiscard]] uint32_t lastUnderLoadMillivolts() const {
        return _lastUnderLoadMillivolts;
    }
    [[nodiscard]] uint32_t persistedIntervals() const {
        return _persistedIntervals;
    }

    void requestFooterIfTerminal() {
        if ((_state == BatteryCalibrationState::Invalid ||
             _state == BatteryCalibrationState::Aborted) &&
            _headerWritten) {
            _footerPending = true;
        }
    }

  private:
    struct IntervalAccumulator {
        int64_t voltageSum = 0;
        int64_t currentSum = 0;
        int64_t powerSum = 0;
        uint32_t minimumVoltage = std::numeric_limits<uint32_t>::max();
        uint32_t maximumVoltage = 0;
        uint32_t sampleCount = 0;
        uint32_t maximumGapMs = 0;
    };

    void resetRuntime() {
        _state = BatteryCalibrationState::Idle;
        _reason = BatteryCalibrationInvalidReason::None;
        _sessionId = 0;
        _startedAtMs = 0;
        _started = false;
        _lastMeasurementAtMs = 0;
        _intervalStartedAtMs = 0;
        _lastUnderLoadMillivolts = 0;
        _observedCutoffMillivolts = 0;
        _persistedIntervals = 0;
        _headerPending = false;
        _headerWritten = false;
        _footerPending = false;
        _cutoffCandidateAtMs.reset();
        _loadRemovedCandidateAtMs.reset();
        _pendingInterval.reset();
        _interval = {};
    }

    void invalidate(BatteryCalibrationInvalidReason reason) {
        _state = BatteryCalibrationState::Invalid;
        _reason = reason;
        _footerPending = _headerWritten;
    }

    void addToInterval(const BatteryMeasurement &measurement,
                       uint32_t sampleGapMs) {
        _interval.voltageSum += measurement.voltageMillivolts;
        _interval.currentSum += measurement.currentMicroamps;
        _interval.powerSum += measurement.powerMilliwatts;
        _interval.minimumVoltage =
            std::min(_interval.minimumVoltage, measurement.voltageMillivolts);
        _interval.maximumVoltage =
            std::max(_interval.maximumVoltage, measurement.voltageMillivolts);
        _interval.maximumGapMs = std::max(_interval.maximumGapMs, sampleGapMs);
        ++_interval.sampleCount;
    }

    void queueInterval(const BatteryStatus &status) {
        if (_persistedIntervals >= _config->maximumIntervalRecords()) {
            invalidate(BatteryCalibrationInvalidReason::DurationExceeded);
            return;
        }
        if (_pendingInterval.has_value()) {
            invalidate(BatteryCalibrationInvalidReason::StorageError);
            return;
        }
        _pendingInterval = makeInterval(status);
        _interval = {};
        _intervalStartedAtMs = status.latestMeasurement->capturedAtMs;
    }

    void queuePartialInterval(const BatteryStatus &status) {
        if (_interval.sampleCount != 0) {
            queueInterval(status);
        }
    }

    [[nodiscard]] IntervalRecordData
    makeInterval(const BatteryStatus &status) const {
        const auto count = static_cast<int64_t>(_interval.sampleCount);
        return {
            .elapsedSeconds = elapsedMs(status.latestMeasurement->capturedAtMs,
                                        _startedAtMs) /
                              1'000U,
            .averageVoltageMillivolts = static_cast<uint32_t>(
                count == 0 ? 0 : _interval.voltageSum / count),
            .minimumVoltageMillivolts =
                count == 0 ? 0 : _interval.minimumVoltage,
            .maximumVoltageMillivolts = _interval.maximumVoltage,
            .averageCurrentMicroamps = static_cast<int32_t>(
                count == 0 ? 0 : _interval.currentSum / count),
            .averagePowerMilliwatts = static_cast<int32_t>(
                count == 0 ? 0 : _interval.powerSum / count),
            .cumulativeMilliampHours = status.dischargedMilliampHours,
            .cumulativeMilliwattHours = status.dischargedMilliwattHours,
            .sampleCount = _interval.sampleCount,
            .maximumGapMs = _interval.maximumGapMs,
            .lastUnderLoadMillivolts = _lastUnderLoadMillivolts,
        };
    }

    [[nodiscard]] static constexpr uint32_t absoluteCurrent(int32_t current) {
        if (current >= 0) {
            return static_cast<uint32_t>(current);
        }
        return static_cast<uint32_t>(-(static_cast<int64_t>(current)));
    }

    const BatteryConfig *_config = nullptr;
    IntervalAccumulator _interval{};
    std::optional<IntervalRecordData> _pendingInterval{};
    std::optional<uint32_t> _cutoffCandidateAtMs{};
    std::optional<uint32_t> _loadRemovedCandidateAtMs{};
    BatteryCalibrationState _state = BatteryCalibrationState::Idle;
    BatteryCalibrationInvalidReason _reason =
        BatteryCalibrationInvalidReason::None;
    uint32_t _sessionId = 0;
    uint32_t _startedAtMs = 0;
    uint32_t _lastMeasurementAtMs = 0;
    uint32_t _intervalStartedAtMs = 0;
    uint32_t _lastUnderLoadMillivolts = 0;
    uint32_t _observedCutoffMillivolts = 0;
    uint32_t _persistedIntervals = 0;
    bool _headerPending = false;
    bool _headerWritten = false;
    bool _footerPending = false;
    bool _started = false;
};

} // namespace Totem::BatteryMonitor::detail
