// IWYU pragma: private

#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasMutex.hpp"
#include "BatteryMonitor/Interfaces/Config.hpp"
#include "BatteryMonitor/Interfaces/Measurement.hpp"
#include "BatteryMonitor/Interfaces/Types.hpp"
#include "BatteryMonitor/detail/Calibration.hpp"
#include "BatteryMonitor/detail/Estimator.hpp"
#include "BatteryMonitor/detail/Journal.hpp"
#include "BatteryMonitor/detail/Metrics.hpp"
#include "BatteryMonitor/detail/StartRequest.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <expected>
#include <limits>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <string_view>

namespace Totem::BatteryMonitor::detail {

/**
 * Sensor-independent battery estimator and calibration state machine.
 *
 * `observe()` and `work()` are non-reentrant and must be called by the same
 * owner task. `observe()` is bounded and performs no filesystem I/O, making it
 * suitable for a synchronous INA2xx sample callback. Cross-task control uses a
 * one-entry atomic mailbox; status readers only contend with a zero-wait owner
 * publication of a fixed-size snapshot.
 */
class BatteryMonitor : public HasLifecycle<BatteryMonitor, BatteryConfig>,
                       public HasMutex<BatteryMonitor> {
    using Self = BatteryMonitor;

    friend class HasLifecycle<Self, BatteryConfig>;
    friend struct LifecycleContract<Self, BatteryConfig>;
    friend struct MutexContract<Self>;

    enum class ControlRequest : uint8_t {
        None,
        StartCalibration,
        AbortCalibration,
    };

  public:
    DELETE_COPY(BatteryMonitor)
    DELETE_MOVE(BatteryMonitor)

    static constexpr const char *name = "BatteryMonitor";
    static constexpr LogComponent logComponent = LogComponent::System;

    BatteryMonitor() = default;

    /**
     * Consumes one valid sensor sample as a domain observation.
     *
     * Absent, practical-limit, and absolute-limit source states are successful
     * values. Only lifecycle or internal processing failures use the error arm.
     */
    [[nodiscard]] std::expected<BatteryObservation, ReturnCode>
    observe(const BatteryMeasurement &measurement) {
        FAIL_IF_INACTIVE_UNEXPECTED(
            "Cannot observe with inactive battery monitor");

        const auto previousSource = _estimator.status().sourceState;
        const auto previousCalibration = _calibration.state();
        const auto estimatorObservation = _estimator.observe(measurement);
        const auto calibrationObservation = _calibration.observe(
            measurement, estimatorObservation, _estimator.status());
        if (calibrationObservation.started) {
            _estimator.resetFromFull();
            _log_i("Battery calibration session %" PRIu32 " started at %" PRIu32
                   " mV and %" PRId32 " uA",
                   _calibration.sessionId(), measurement.voltageMillivolts,
                   measurement.currentMicroamps);
        }

        const auto currentSource = _estimator.status().sourceState;
        if (previousSource != currentSource) {
            logSourceTransition(currentSource, measurement.voltageMillivolts);
        }
        if (previousCalibration != _calibration.state()) {
            logCalibrationTransition();
        }
        if (estimatorObservation.sampleGapExceeded ||
            calibrationObservation.invalidated) {
            _metrics->addFailure();
        }

        const auto currentStatus = composeStatus();
        _metrics->record(currentStatus);
        publishStatus(currentStatus);
        return BatteryObservation{
            .sourceState = currentSource,
            .calibrationState = _calibration.state(),
            .calibrationReason = _calibration.reason(),
            .elapsedMs = estimatorObservation.elapsedMs,
            .sampleGapExceeded = estimatorObservation.sampleGapExceeded,
            .chargingDetected = estimatorObservation.chargingDetected,
            .integrated = estimatorObservation.integrated,
        };
    }

    /**
     * Runs owner-task maintenance and at most one bounded journal step.
     * Filesystem calls are never made while the status snapshot lock is held.
     */
    ReturnCode work(uint32_t nowMs) {
        FAIL_IF_INACTIVE_ERR("Cannot work inactive battery monitor");

        if (_estimator.checkFreshness(nowMs)) {
            _metrics->addFailure();
            _log_w("Battery measurement became stale after %" PRIu32 " ms",
                   config().sampleGapToleranceMs);
        }
        processOneControlRequest();

        const auto previousCalibration = _calibration.state();
        _calibration.checkSensorTimeout(nowMs, _estimator.status());
        _calibration.requestFooterIfTerminal();
        if (previousCalibration != _calibration.state()) {
            logCalibrationTransition();
            _metrics->addFailure();
        }

        const auto ret = workOnePersistenceStep(nowMs);
        const auto currentStatus = composeStatus();
        _metrics->record(currentStatus);
        publishStatus(currentStatus);
        return ret;
    }

    /** Copies the last published snapshot or reports lock/lifecycle failure. */
    [[nodiscard]] std::expected<BatteryStatus, ReturnCode> status() const {
        FAIL_IF_INACTIVE_UNEXPECTED(
            "Cannot read inactive battery monitor status");
        auto guard = _mutexGuard(2);
        if (!guard.acquired()) {
            return std::unexpected(ERR(CoreError, Timeout));
        }
        return _publishedStatus;
    }

    /**
     * Enqueues calibration start for the owner task and returns its immediate
     * preflight reason without mutating owner-task calibration state. A full
     * mailbox reports storage backpressure and never overwrites an earlier
     * request.
     */
    [[nodiscard]] BatteryCalibrationStartResult requestCalibrationStart() {
        FAIL_IF_INACTIVE((BatteryCalibrationStartResult{
                             .error = ERR(LifecycleError, NotActive),
                             .reason = BatteryCalibrationInvalidReason::None,
                         }),
                         "Cannot calibrate with inactive battery monitor");
        auto snapshot = status();
        if (!snapshot) {
            return {
                .error = snapshot.error(),
                .reason = BatteryCalibrationInvalidReason::None,
            };
        }
        auto result = assessCalibrationStart(*snapshot, config());
        if (!result.queued()) {
            return result;
        }
        result.error = enqueue(ControlRequest::StartCalibration);
        return result;
    }

    ReturnCode startCalibration() { return requestCalibrationStart().error; }

    /** Enqueues calibration abort for the owner task. */
    ReturnCode abortCalibration() {
        FAIL_IF_INACTIVE_ERR("Cannot abort inactive battery monitor");
        auto snapshot = status();
        if (!snapshot) {
            return snapshot.error();
        }
        const auto state = snapshot->calibrationState;
        if (state != BatteryCalibrationState::ArmedFull &&
            state != BatteryCalibrationState::Discharging &&
            state != BatteryCalibrationState::Finalizing) {
            return ERR(CoreError, InvalidState);
        }
        return enqueue(ControlRequest::AbortCalibration);
    }

  private:
    ReturnCode _onBegin() {
        FAIL_IF_ERR_FWD(prepareMetrics(), "Failed to prepare battery metrics");
        FAIL_IF_ERR_FWD(_journal.begin(config()),
                        "Failed to initialize battery journal path");
        _calibration.begin(config());
        _journalStatus = {};
        _curveBuilder.reset();
        _nextRecordSequence = 0;
        _recordsChecksum = 0;
        _nextProfilePoint = 0;
        _rebootFooterPending = false;
        _storageHealth = BatteryStorageHealth::Unavailable;
        _request.store(ControlRequest::None, std::memory_order_relaxed);

        const BatteryProfile *profile = nullptr;
        if (FileSystemService::configured()) {
            auto scan = _journal.scan(config());
            if (!scan) {
                _storageHealth = BatteryStorageHealth::Corrupt;
                _log_e("Battery profile scan failed: " ERR_FMT,
                       ERR_ARG(scan.error()));
                _metrics->addFileSystemFailure();
            } else {
                _journalStatus = *scan;
                if (scan->issue == JournalScanIssue::Oversized) {
                    _storageHealth = BatteryStorageHealth::Full;
                    _log_e("Battery journal exceeds the configured %" PRIu32
                           " record bound",
                           config().maximumJournalRecords);
                } else if (scan->issue == JournalScanIssue::Corrupt) {
                    _storageHealth = BatteryStorageHealth::Corrupt;
                    _log_e("Battery journal is corrupt; calibration writes "
                           "are disabled");
                } else {
                    auto capacity = _journal.canFitSession(config());
                    if (!capacity) {
                        _storageHealth = BatteryStorageHealth::Corrupt;
                        _metrics->addFileSystemFailure();
                        _log_e(
                            "Battery journal capacity check failed: " ERR_FMT,
                            ERR_ARG(capacity.error()));
                    } else {
                        _storageHealth = *capacity
                                             ? BatteryStorageHealth::Healthy
                                             : BatteryStorageHealth::Full;
                    }
                }

                if ((_storageHealth == BatteryStorageHealth::Healthy ||
                     _storageHealth == BatteryStorageHealth::Full) &&
                    _journalStatus.latestProfile.status.active) {
                    profile = &_journalStatus.latestProfile;
                }
                if (_storageHealth == BatteryStorageHealth::Healthy &&
                    scan->danglingSessionId != 0) {
                    _rebootFooterPending = true;
                    _log_w("Battery calibration session %" PRIu32
                           " was interrupted by reboot and will remain "
                           "inactive",
                           scan->danglingSessionId);
                }
                if (scan->corruptRecordCount != 0) {
                    _log_w("Battery journal contains %" PRIu32
                           " corrupt record(s); calibration is disabled",
                           scan->corruptRecordCount);
                }
            }
        } else {
            _log_w("Battery profile storage unavailable; runtime estimation "
                   "uses nominal capacity and calibration is disabled");
        }

        _estimator.begin(config(), profile);
        _metrics->initialize();
        const auto initialStatus = composeStatus();
        _metrics->record(initialStatus);
        publishStatus(initialStatus);

        _log_i("Initialized battery monitor for %uS%uP Li-ion; nominal=%" PRIu32
               " mAh/%" PRIu32 " mWh, profile=%s, storage=" SV_FMT,
               static_cast<unsigned>(config().seriesCells),
               static_cast<unsigned>(config().parallelCells),
               config().nominalPackCapacityMilliampHours,
               config().nominalPackEnergyMilliwattHours(),
               profile != nullptr ? "learned" : "idealized",
               MAGIC_SV_ARG(_storageHealth));
        return OK();
    }

    ReturnCode _onEnd() {
        _calibration.begin(config());
        _estimator.reset();
        _journalStatus = {};
        _curveBuilder.reset();
        _nextRecordSequence = 0;
        _recordsChecksum = 0;
        _nextProfilePoint = 0;
        _rebootFooterPending = false;
        _request.store(ControlRequest::None, std::memory_order_relaxed);
        _storageHealth = BatteryStorageHealth::Unavailable;
        _journal.end();
        if (_metrics.has_value()) {
            _metrics->initialize();
        }
        return OK();
    }

    ReturnCode prepareMetrics() {
        const std::string_view requestedName{config().metricsGroupName};
        if (_metrics.has_value()) {
            FAIL_IF(std::string_view{_metricsName.data()} != requestedName,
                    ERR(CoreError, InvalidArgument),
                    "Battery metrics group name cannot change after first "
                    "begin");
            return OK();
        }
        for (size_t i = 0; i < requestedName.size(); ++i) {
            _metricsName[i] = requestedName[i];
        }
        _metricsName[requestedName.size()] = '\0';
        _metricsGroupDesc.name = _metricsName.data();
        _metrics.emplace(Metrics::create(_metricsGroupDesc));
        return OK();
    }

    [[nodiscard]] BatteryStatus composeStatus() const {
        auto result = _estimator.status();
        result.calibrationState = _calibration.state();
        result.calibrationReason = _calibration.reason();
        result.persistedCalibrationIntervals =
            _calibration.persistedIntervals();
        result.storageHealth = _storageHealth;
        result.catalog = {
            .highestSessionId = _journalStatus.highestSessionId,
            .completeSessionCount = _journalStatus.completeSessionCount,
            .incompatibleSessionCount = _journalStatus.incompatibleSessionCount,
            .incompleteSessionCount = _journalStatus.incompleteSessionCount,
            .corruptRecordCount = _journalStatus.corruptRecordCount,
            .journalRecordCount = _journalStatus.journalRecordCount,
        };
        if (_journalStatus.latestProfile.status.active) {
            result.profile = _journalStatus.latestProfile.status;
        }
        return result;
    }

    void publishStatus(const BatteryStatus &status) {
        // Zero-wait publication ensures an INA callback never waits for a
        // status reader. A skipped publication is refreshed by the next owner
        // call; readers continue to receive the preceding coherent snapshot.
        auto guard = _mutexGuard(0);
        if (guard.acquired()) {
            _publishedStatus = status;
        }
    }

    ReturnCode enqueue(ControlRequest request) {
        auto expected = ControlRequest::None;
        if (!_request.compare_exchange_strong(expected, request,
                                              std::memory_order_release,
                                              std::memory_order_relaxed)) {
            return ERR(StorageError, Backpressure);
        }
        return OK();
    }

    void processOneControlRequest() {
        const auto request =
            _request.exchange(ControlRequest::None, std::memory_order_acq_rel);
        switch (request) {
        case ControlRequest::None:
            return;
        case ControlRequest::StartCalibration:
            startCalibrationOwned();
            return;
        case ControlRequest::AbortCalibration:
            abortCalibrationOwned();
            return;
        }
    }

    void startCalibrationOwned() {
        if (_storageHealth != BatteryStorageHealth::Healthy) {
            _calibration.rejectStart(
                BatteryCalibrationInvalidReason::StorageUnavailable);
            return;
        }
        const auto &estimatorStatus = _estimator.status();
        if (!estimatorStatus.latestMeasurement.has_value() ||
            estimatorStatus.measurementFreshness !=
                BatteryMeasurementFreshness::Fresh) {
            _calibration.rejectStart(
                BatteryCalibrationInvalidReason::SensorTimeout);
            return;
        }
        auto capacity = _journal.canFitSession(config());
        if (!capacity) {
            _storageHealth = BatteryStorageHealth::Corrupt;
            _calibration.rejectStart(
                BatteryCalibrationInvalidReason::StorageError);
            _metrics->addFileSystemFailure();
            return;
        }
        if (!*capacity) {
            _storageHealth = BatteryStorageHealth::Full;
            _calibration.rejectStart(
                BatteryCalibrationInvalidReason::StorageUnavailable);
            return;
        }
        if (_journalStatus.highestSessionId ==
            std::numeric_limits<uint32_t>::max()) {
            _calibration.rejectStart(
                BatteryCalibrationInvalidReason::StorageError);
            return;
        }

        const uint32_t sessionId = _journalStatus.highestSessionId + 1U;
        const auto ret = _calibration.arm(estimatorStatus, sessionId);
        if (!ret.ok()) {
            return;
        }
        _journalStatus.highestSessionId = sessionId;
        _nextRecordSequence = 0;
        _recordsChecksum = 0;
        _curveBuilder.reset();
        _nextProfilePoint = 0;
        _log_i("Battery calibration request accepted for session %" PRIu32,
               sessionId);
    }

    void abortCalibrationOwned() {
        const auto ret = _calibration.abort(_estimator.status());
        if (!ret.ok()) {
            return;
        }
        // An abort can arrive while the journal is being scanned to build the
        // profile. Close that reader before appending the terminal footer.
        _curveBuilder.reset();
        _nextProfilePoint = 0;
        _log_w("Battery calibration abort request accepted");
    }

    ReturnCode workOnePersistenceStep(uint32_t nowMs) {
        if (_rebootFooterPending) {
            return writeRebootFooter();
        }
        if (_calibration.headerPending()) {
            return writeHeader();
        }
        if (_calibration.intervalPending()) {
            return writeInterval();
        }
        if (_calibration.state() == BatteryCalibrationState::Finalizing) {
            return workFinalization(nowMs);
        }
        if (_calibration.footerPending()) {
            return writeTerminalFooter(nowMs);
        }
        return OK();
    }

    ReturnCode writeHeader() {
        const auto payload =
            makeHeaderPayload(config(), _calibration.startedAtMs());
        auto record = makeJournalRecord(JournalRecordType::SessionHeader,
                                        _nextRecordSequence,
                                        _calibration.sessionId(), payload);
        auto ret = _journal.append(record);
        if (!ret.ok()) {
            handleStorageFailure(ret);
            return ret;
        }
        _recordsChecksum = getU32(record, journalCrcOffset);
        ++_nextRecordSequence;
        ++_journalStatus.journalRecordCount;
        _calibration.markHeaderWritten();
        return OK();
    }

    ReturnCode writeInterval() {
        const auto interval = _calibration.pendingInterval();
        const auto payload = makeIntervalPayload(interval);
        auto record =
            makeJournalRecord(JournalRecordType::Interval, _nextRecordSequence,
                              _calibration.sessionId(), payload);
        auto ret = _journal.append(record);
        if (!ret.ok()) {
            handleStorageFailure(ret);
            return ret;
        }
        _recordsChecksum ^= getU32(record, journalCrcOffset);
        ++_nextRecordSequence;
        ++_journalStatus.journalRecordCount;
        _calibration.markIntervalWritten();
        _metrics->addCalibrationInterval();
        _log_i("Battery cal interval: sid=%" PRIu32 " n=%" PRIu32
               " elapsed=%" PRIu32 "s samples=%" PRIu32 " maxGap=%" PRIu32 "ms",
               _calibration.sessionId(), _calibration.persistedIntervals(),
               interval.elapsedSeconds, interval.sampleCount,
               interval.maximumGapMs);
        _log_i(
            "Battery cal values: sid=%" PRIu32 " n=%" PRIu32 " V=%" PRIu32
            "/%" PRIu32 "/%" PRIu32 "mV I=%" PRId32 "uA P=%" PRId32
            "mW used=%" PRIu32 "mAh/%" PRIu32 "mWh",
            _calibration.sessionId(), _calibration.persistedIntervals(),
            interval.averageVoltageMillivolts,
            interval.minimumVoltageMillivolts,
            interval.maximumVoltageMillivolts, interval.averageCurrentMicroamps,
            interval.averagePowerMilliwatts, interval.cumulativeMilliampHours,
            interval.cumulativeMilliwattHours);
        return OK();
    }

    ReturnCode workFinalization(uint32_t nowMs) {
        const auto estimatorStatus = _estimator.status();
        if (!_curveBuilder.has_value()) {
            _curveBuilder.emplace();
            const auto ret = _journal.beginCurveBuild(
                _calibration.sessionId(),
                estimatorStatus.dischargedMilliampHours, *_curveBuilder);
            if (!ret.ok()) {
                handleStorageFailure(ret);
                return ret;
            }
            return OK();
        }

        if (_curveBuilder->active) {
            auto completed = _journal.workCurveBuild(
                *_curveBuilder, config().curveBuildRecordsPerWork);
            if (!completed) {
                const auto error = completed.error();
                handleStorageFailure(error);
                return error;
            }
            if (!*completed) {
                return OK();
            }
            _nextProfilePoint = 0;
            return OK();
        }

        if (_nextProfilePoint < profilePointCount) {
            const ProfilePointRecordData point{
                .stateOfChargePercent = _nextProfilePoint,
                .loadedVoltageMillivolts =
                    _curveBuilder->result.points[_nextProfilePoint]
                        .loadedVoltageMillivolts,
                .representativeCurrentMicroamps =
                    _curveBuilder->result.points[_nextProfilePoint]
                        .representativeCurrentMicroamps,
            };
            auto record = makeJournalRecord(
                JournalRecordType::ProfilePoint, _nextRecordSequence,
                _calibration.sessionId(), makeProfilePointPayload(point));
            auto ret = _journal.append(record);
            if (!ret.ok()) {
                handleStorageFailure(ret);
                return ret;
            }
            _recordsChecksum ^= getU32(record, journalCrcOffset);
            ++_nextRecordSequence;
            ++_nextProfilePoint;
            ++_journalStatus.journalRecordCount;
            return OK();
        }

        const FooterRecordData footer{
            .state = BatteryCalibrationState::Complete,
            .reason = BatteryCalibrationInvalidReason::None,
            .pointCount = profilePointCount,
            .usableMilliampHours = estimatorStatus.dischargedMilliampHours,
            .usableMilliwattHours = estimatorStatus.dischargedMilliwattHours,
            .durationSeconds = _calibration.durationMs(nowMs) / 1'000U,
            .observedCutoffMillivolts = _calibration.observedCutoffMillivolts(),
            .lastUnderLoadMillivolts = _calibration.lastUnderLoadMillivolts(),
            .intervalCount = _calibration.persistedIntervals(),
            .maximumGapMs = estimatorStatus.maximumSampleGapMs,
            .recordsChecksum = _recordsChecksum,
        };
        auto record = makeJournalRecord(
            JournalRecordType::SessionFooter, _nextRecordSequence,
            _calibration.sessionId(), makeFooterPayload(footer));
        auto ret = _journal.append(record);
        if (!ret.ok()) {
            handleStorageFailure(ret);
            return ret;
        }
        ++_journalStatus.journalRecordCount;

        BatteryProfile completed{};
        completed.status = {
            .sessionId = _calibration.sessionId(),
            .usableMilliampHours = estimatorStatus.dischargedMilliampHours,
            .usableMilliwattHours = estimatorStatus.dischargedMilliwattHours,
            .durationSeconds = footer.durationSeconds,
            .observedCutoffMillivolts = footer.observedCutoffMillivolts,
            .pointCount = profilePointCount,
            .active = true,
        };
        completed.points = _curveBuilder->result.points;
        _journalStatus.latestProfile = completed;
        ++_journalStatus.completeSessionCount;
        _calibration.markComplete();
        _log_i("Battery calibration complete: session=%" PRIu32
               ", measured=%" PRIu32 " mAh/%" PRIu32 " mWh, duration=%" PRIu32
               " s",
               completed.status.sessionId, completed.status.usableMilliampHours,
               completed.status.usableMilliwattHours,
               completed.status.durationSeconds);
        _log_i("Battery cal quality: intervals=%" PRIu32 " maxGap=%" PRIu32
               "ms cutoff=%" PRIu32 "mV lastLoad=%" PRIu32 "mV",
               footer.intervalCount, footer.maximumGapMs,
               footer.observedCutoffMillivolts, footer.lastUnderLoadMillivolts);
        return OK();
    }

    ReturnCode writeTerminalFooter(uint32_t nowMs) {
        const auto estimatorStatus = _estimator.status();
        const FooterRecordData footer{
            .state = _calibration.state(),
            .reason = _calibration.reason(),
            .pointCount = 0,
            .usableMilliampHours = estimatorStatus.dischargedMilliampHours,
            .usableMilliwattHours = estimatorStatus.dischargedMilliwattHours,
            .durationSeconds = _calibration.durationMs(nowMs) / 1'000U,
            .observedCutoffMillivolts = _calibration.observedCutoffMillivolts(),
            .lastUnderLoadMillivolts = _calibration.lastUnderLoadMillivolts(),
            .intervalCount = _calibration.persistedIntervals(),
            .maximumGapMs = estimatorStatus.maximumSampleGapMs,
            .recordsChecksum = _recordsChecksum,
        };
        auto record = makeJournalRecord(
            JournalRecordType::SessionFooter, _nextRecordSequence,
            _calibration.sessionId(), makeFooterPayload(footer));
        auto ret = _journal.append(record);
        if (!ret.ok()) {
            handleStorageFailure(ret);
            return ret;
        }
        ++_journalStatus.journalRecordCount;
        _calibration.markFooterWritten();
        ++_journalStatus.incompleteSessionCount;
        return OK();
    }

    ReturnCode writeRebootFooter() {
        const FooterRecordData footer{
            .state = BatteryCalibrationState::Invalid,
            .reason = BatteryCalibrationInvalidReason::Rebooted,
            .recordsChecksum = _journalStatus.danglingRecordsChecksum,
        };
        auto record = makeJournalRecord(JournalRecordType::SessionFooter,
                                        _journalStatus.danglingNextSequence,
                                        _journalStatus.danglingSessionId,
                                        makeFooterPayload(footer));
        // Consume before attempting so a failed write is never retried on each
        // loop iteration.
        _rebootFooterPending = false;
        auto ret = _journal.append(record);
        if (!ret.ok()) {
            handleStorageFailure(ret);
            return ret;
        }
        ++_journalStatus.journalRecordCount;
        _journalStatus.danglingSessionId = 0;
        _journalStatus.danglingNextSequence = 0;
        _journalStatus.danglingRecordsChecksum = 0;
        _log_w("Recorded interrupted battery calibration as invalid");
        return OK();
    }

    void handleStorageFailure(ReturnCode error) {
        _metrics->addFileSystemFailure();
        _rebootFooterPending = false;
        _curveBuilder.reset();
        _calibration.storageFailed();
        if (error == ERR(CoreError, Overflow)) {
            _storageHealth = BatteryStorageHealth::Full;
        } else if (error == ERR(CoreError, InvalidData) ||
                   error == ERR(CoreError, CrcError)) {
            _storageHealth = BatteryStorageHealth::Corrupt;
        } else {
            _storageHealth = BatteryStorageHealth::WriteFailed;
        }
        _log_e("Battery calibration storage failure: " ERR_FMT, ERR_ARG(error));
    }

    void logSourceTransition(BatterySourceState source,
                             uint32_t voltageMillivolts) const {
        switch (source) {
        case BatterySourceState::Unknown:
            return;
        case BatterySourceState::Absent:
            _log_w("Battery source absent or BMS open at %" PRIu32 " mV",
                   voltageMillivolts);
            return;
        case BatterySourceState::AbsoluteUnder:
        case BatterySourceState::AbsoluteOver:
            _log_e("Battery absolute voltage state=" SV_FMT " at %" PRIu32
                   " mV",
                   MAGIC_SV_ARG(source), voltageMillivolts);
            return;
        case BatterySourceState::PracticalUnder:
        case BatterySourceState::PracticalOver:
            _log_w("Battery practical voltage state=" SV_FMT " at %" PRIu32
                   " mV",
                   MAGIC_SV_ARG(source), voltageMillivolts);
            return;
        case BatterySourceState::Normal:
            _log_i("Battery voltage returned to normal at %" PRIu32 " mV",
                   voltageMillivolts);
            return;
        }
    }

    void logCalibrationTransition() const {
        const auto state = _calibration.state();
        if (state == BatteryCalibrationState::Invalid ||
            state == BatteryCalibrationState::Aborted) {
            _log_w("Battery calibration state=" SV_FMT ", reason=" SV_FMT,
                   MAGIC_SV_ARG(state), MAGIC_SV_ARG(_calibration.reason()));
            return;
        }
        _log_i("Battery calibration state=" SV_FMT, MAGIC_SV_ARG(state));
    }

    Estimator _estimator{};
    Calibration _calibration{};
    Journal _journal{};
    JournalScanResult _journalStatus{};
    std::optional<CurveBuilder> _curveBuilder{};
    BatteryStatus _publishedStatus{};
    std::atomic<ControlRequest> _request{ControlRequest::None};
    BatteryStorageHealth _storageHealth = BatteryStorageHealth::Unavailable;
    uint32_t _nextRecordSequence = 0;
    uint32_t _recordsChecksum = 0;
    uint16_t _nextProfilePoint = 0;
    bool _rebootFooterPending = false;

    std::array<char, MetricLimits::maxMetricGroupNameLength + 1U>
        _metricsName{};
    MetricsBackend::MetricGroupDesc _metricsGroupDesc{
        .name = _metricsName.data(),
        .level = MetricsBackend::MetricLevel::Baseline,
    };
    std::optional<Metrics> _metrics{};
};

inline constexpr LifecycleContract<BatteryMonitor, BatteryConfig>
    _battery_monitor_lifecycle_contract;
inline constexpr MutexContract<BatteryMonitor> _battery_monitor_mutex_contract;

} // namespace Totem::BatteryMonitor::detail
