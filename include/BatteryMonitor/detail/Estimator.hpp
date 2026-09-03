// IWYU pragma: private

#pragma once

#include "BatteryMonitor/Interfaces/Config.hpp"
#include "BatteryMonitor/Interfaces/Measurement.hpp"
#include "BatteryMonitor/Interfaces/Types.hpp"
#include "BatteryMonitor/detail/Arithmetic.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace Totem::BatteryMonitor::detail {

struct EstimatorObservation {
    uint32_t elapsedMs = 0;
    bool sampleGapExceeded = false;
    bool chargingDetected = false;
    bool integrated = false;
};

class Estimator {
  public:
    void begin(const BatteryConfig &config,
               const BatteryProfile *profile = nullptr) {
        _config = &config;
        _profile = profile;
        _totalMilliampHours = hasLearnedProfile()
                                  ? profile->status.usableMilliampHours
                                  : config.nominalPackCapacityMilliampHours;
        _totalMilliwattHours = hasLearnedProfile()
                                   ? profile->status.usableMilliwattHours
                                   : config.nominalPackEnergyMilliwattHours();
        reset();
    }

    void reset() {
        _status = {};
        _status.profile =
            hasLearnedProfile() ? _profile->status : BatteryProfileStatus{};
        _status.sourceState = BatterySourceState::Unknown;
        _status.measurementFreshness =
            BatteryMeasurementFreshness::NeverReceived;
        _status.confidence = BatteryEstimateConfidence::Unavailable;
        _hasPrevious = false;
        _initialized = false;
        _chargeRemainder = 0;
        _energyRemainder = 0;
        _averagePowerRemainder = 0;
        _initialRemainingMilliampHours = 0;
        _initialRemainingMilliwattHours = 0;
    }

    void resetFromFull() {
        _initialized = true;
        _initialRemainingMilliampHours = _totalMilliampHours;
        _initialRemainingMilliwattHours = _totalMilliwattHours;
        _status.stateOfChargePartsPerThousand = 1'000;
        _status.remainingMilliampHours = _totalMilliampHours;
        _status.remainingMilliwattHours = _totalMilliwattHours;
        _status.dischargedMilliampHours = 0;
        _status.dischargedMilliwattHours = 0;
        _status.averagePowerMilliwatts = 0;
        _status.timeToEmptyMinutes.reset();
        _chargeRemainder = 0;
        _energyRemainder = 0;
        _averagePowerRemainder = 0;
        _status.confidence = hasLearnedProfile()
                                 ? BatteryEstimateConfidence::IntegratedLearned
                                 : BatteryEstimateConfidence::IntegratedNominal;
    }

    [[nodiscard]] EstimatorObservation
    observe(const BatteryMeasurement &measurement) {
        EstimatorObservation observation{};
        const auto previousSourceState = _status.sourceState;
        _status.latestMeasurement = measurement;
        _status.measurementFreshness = BatteryMeasurementFreshness::Fresh;
        _status.sampleCount = saturatingAdd(_status.sampleCount, 1);
        _status.sourceState = classifySource(measurement.voltageMillivolts);

        if (_hasPrevious) {
            observation.elapsedMs = elapsedMs(
                measurement.capturedAtMs, _previousMeasurement.capturedAtMs);
            _status.maximumSampleGapMs =
                std::max(_status.maximumSampleGapMs, observation.elapsedMs);
            observation.sampleGapExceeded = detail::sampleGapExceeded(
                observation.elapsedMs, _config->sampleGapToleranceMs);
        }

        const bool usableVoltage =
            _status.sourceState == BatterySourceState::PracticalUnder ||
            _status.sourceState == BatterySourceState::Normal ||
            _status.sourceState == BatterySourceState::PracticalOver;
        if (!usableVoltage) {
            _status.timeToEmptyMinutes.reset();
            _hasPrevious = false;
            return observation;
        }

        if (!_initialized || !isUsableSource(previousSourceState)) {
            resetSessionIntegration();
            initializeFromMeasurement(measurement);
        }

        if (_hasPrevious && observation.sampleGapExceeded) {
            resetSessionIntegration();
            initializeFromMeasurement(measurement);
            _hasPrevious = false;
        }

        observation.chargingDetected = detail::chargingDetected(
            measurement.currentMicroamps, _config->currentDeadbandMicroamps);

        if (_hasPrevious && !observation.chargingDetected &&
            !detail::chargingDetected(_previousMeasurement.currentMicroamps,
                                      _config->currentDeadbandMicroamps)) {
            observation.integrated = integrateInterval(
                _previousMeasurement, measurement, observation.elapsedMs);
        }

        updateAveragePower(measurement, observation.sampleGapExceeded
                                            ? 0U
                                            : observation.elapsedMs);
        _previousMeasurement = measurement;
        _hasPrevious = true;
        updateDerivedStatus(observation.chargingDetected);
        return observation;
    }

    /** Invalidates time-dependent estimates after the sensor freshness bound.
     */
    [[nodiscard]] bool checkFreshness(uint32_t nowMs) {
        if (!_status.latestMeasurement.has_value() ||
            _status.measurementFreshness !=
                BatteryMeasurementFreshness::Fresh) {
            return false;
        }
        if (elapsedMs(nowMs, _status.latestMeasurement->capturedAtMs) <=
            _config->sampleGapToleranceMs) {
            return false;
        }

        _status.measurementFreshness = BatteryMeasurementFreshness::Stale;
        _status.timeToEmptyMinutes.reset();
        _hasPrevious = false;
        _initialized = false;
        return true;
    }

    [[nodiscard]] BatterySourceState
    classifySource(uint32_t voltageMillivolts) const {
        if (voltageMillivolts <= _config->disconnectedVoltageMillivolts) {
            return BatterySourceState::Absent;
        }
        if (voltageMillivolts < _config->absoluteMinPackMillivolts()) {
            return BatterySourceState::AbsoluteUnder;
        }
        if (voltageMillivolts < _config->practicalMinPackMillivolts()) {
            return BatterySourceState::PracticalUnder;
        }
        if (voltageMillivolts <= _config->practicalMaxPackMillivolts()) {
            return BatterySourceState::Normal;
        }
        if (voltageMillivolts <= _config->absoluteMaxPackMillivolts()) {
            return BatterySourceState::PracticalOver;
        }
        return BatterySourceState::AbsoluteOver;
    }

    [[nodiscard]] const BatteryStatus &status() const { return _status; }

  private:
    [[nodiscard]] static constexpr bool
    isUsableSource(BatterySourceState state) {
        return state == BatterySourceState::PracticalUnder ||
               state == BatterySourceState::Normal ||
               state == BatterySourceState::PracticalOver;
    }

    void resetSessionIntegration() {
        _status.dischargedMilliampHours = 0;
        _status.dischargedMilliwattHours = 0;
        _status.averagePowerMilliwatts = 0;
        _status.timeToEmptyMinutes.reset();
        _chargeRemainder = 0;
        _energyRemainder = 0;
        _averagePowerRemainder = 0;
        _initialized = false;
    }

    void initializeFromMeasurement(const BatteryMeasurement &measurement) {
        const auto learnedMatch =
            hasLearnedProfile()
                ? findLearnedMatch(measurement.voltageMillivolts)
                : LearnedMatch{};
        const bool matchingCalibrationLoad =
            learnedMatch.valid && measurement.currentMicroamps > 0 &&
            learnedMatch.representativeCurrentMicroamps > 0 &&
            currentWithinTwentyFivePercent(
                measurement.currentMicroamps,
                learnedMatch.representativeCurrentMicroamps);
        const uint32_t soc = matchingCalibrationLoad
                                 ? learnedMatch.socPartsPerThousand
                                 : linearSocPartsPerThousand(
                                       measurement.voltageMillivolts,
                                       _config->practicalMinPackMillivolts(),
                                       _config->practicalMaxPackMillivolts());
        _initialRemainingMilliampHours =
            remainingFromSoc(_totalMilliampHours, soc);
        _initialRemainingMilliwattHours =
            remainingFromSoc(_totalMilliwattHours, soc);
        _status.remainingMilliampHours = _initialRemainingMilliampHours;
        _status.remainingMilliwattHours = _initialRemainingMilliwattHours;
        _status.stateOfChargePartsPerThousand = soc;
        _status.confidence = BatteryEstimateConfidence::VoltageOnly;
        _initialized = true;
    }

    [[nodiscard]] bool integrateInterval(const BatteryMeasurement &previous,
                                         const BatteryMeasurement &current,
                                         uint32_t deltaMs) {
        if (deltaMs == 0 ||
            !dischargingDetected(previous.currentMicroamps,
                                 _config->currentDeadbandMicroamps) ||
            !dischargingDetected(current.currentMicroamps,
                                 _config->currentDeadbandMicroamps)) {
            return false;
        }

        const auto charge = integratePositiveTrapezoid(
            static_cast<uint32_t>(previous.currentMicroamps),
            static_cast<uint32_t>(current.currentMicroamps), deltaMs,
            _chargeRemainder, chargeTrapezoidDivisor);
        if (charge.overflow) {
            return false;
        }

        const uint32_t previousPower =
            previous.powerMilliwatts > 0
                ? static_cast<uint32_t>(previous.powerMilliwatts)
                : 0;
        const uint32_t currentPower =
            current.powerMilliwatts > 0
                ? static_cast<uint32_t>(current.powerMilliwatts)
                : 0;
        const auto energy = integratePositiveTrapezoid(
            previousPower, currentPower, deltaMs, _energyRemainder,
            energyTrapezoidDivisor);
        if (energy.overflow) {
            return false;
        }

        _chargeRemainder = charge.remainder;
        _energyRemainder = energy.remainder;
        _status.dischargedMilliampHours =
            saturatingAdd(_status.dischargedMilliampHours, charge.whole);
        _status.dischargedMilliwattHours =
            saturatingAdd(_status.dischargedMilliwattHours, energy.whole);
        _status.confidence = hasLearnedProfile()
                                 ? BatteryEstimateConfidence::IntegratedLearned
                                 : BatteryEstimateConfidence::IntegratedNominal;
        return true;
    }

    [[nodiscard]] bool hasLearnedProfile() const {
        return _profile != nullptr && _profile->status.active;
    }

    struct LearnedMatch {
        uint32_t socPartsPerThousand = 0;
        int32_t representativeCurrentMicroamps = 0;
        bool valid = false;
    };

    [[nodiscard]] LearnedMatch
    findLearnedMatch(uint32_t voltageMillivolts) const {
        uint32_t bestIndex = 0;
        uint32_t bestDistance = std::numeric_limits<uint32_t>::max();
        for (uint32_t index = 0; index < batteryProfilePointCount; ++index) {
            const auto profileVoltage =
                _profile->points[index].loadedVoltageMillivolts;
            const uint32_t distance = profileVoltage > voltageMillivolts
                                          ? profileVoltage - voltageMillivolts
                                          : voltageMillivolts - profileVoltage;
            if (distance < bestDistance) {
                bestDistance = distance;
                bestIndex = index;
            }
        }
        return {
            .socPartsPerThousand = bestIndex * 10U,
            .representativeCurrentMicroamps =
                _profile->points[bestIndex].representativeCurrentMicroamps,
            .valid = true,
        };
    }

    [[nodiscard]] static bool
    currentWithinTwentyFivePercent(int32_t measured, int32_t reference) {
        const int64_t difference = static_cast<int64_t>(measured) - reference;
        const uint64_t absoluteDifference =
            static_cast<uint64_t>(difference >= 0 ? difference : -difference);
        return absoluteDifference * 4U <= static_cast<uint64_t>(reference);
    }

    void updateAveragePower(const BatteryMeasurement &measurement,
                            uint32_t deltaMs) {
        const uint32_t positivePower =
            measurement.powerMilliwatts > 0
                ? static_cast<uint32_t>(measurement.powerMilliwatts)
                : 0;
        if (_status.averagePowerMilliwatts == 0) {
            _status.averagePowerMilliwatts = positivePower;
            _averagePowerRemainder = 0;
            return;
        }

        const uint32_t weight =
            std::min(deltaMs, _config->averagePowerWindowMs);
        const int64_t difference =
            static_cast<int64_t>(positivePower) -
            static_cast<int64_t>(_status.averagePowerMilliwatts);
        const int64_t numerator =
            difference * static_cast<int64_t>(weight) + _averagePowerRemainder;
        const int64_t adjustment =
            numerator / static_cast<int64_t>(_config->averagePowerWindowMs);
        _averagePowerRemainder =
            numerator % static_cast<int64_t>(_config->averagePowerWindowMs);
        const int64_t updated =
            static_cast<int64_t>(_status.averagePowerMilliwatts) + adjustment;
        const auto clamped = std::clamp<int64_t>(
            updated, 0, std::numeric_limits<uint32_t>::max());
        _status.averagePowerMilliwatts = static_cast<uint32_t>(clamped);
        if (clamped != updated) {
            _averagePowerRemainder = 0;
        }
    }

    void updateDerivedStatus(bool charging) {
        _status.remainingMilliampHours =
            _status.dischargedMilliampHours >= _initialRemainingMilliampHours
                ? 0
                : _initialRemainingMilliampHours -
                      _status.dischargedMilliampHours;
        _status.remainingMilliwattHours =
            _status.dischargedMilliwattHours >= _initialRemainingMilliwattHours
                ? 0
                : _initialRemainingMilliwattHours -
                      _status.dischargedMilliwattHours;
        _status.stateOfChargePartsPerThousand =
            _totalMilliwattHours == 0
                ? 0
                : static_cast<uint32_t>(
                      (static_cast<uint64_t>(_status.remainingMilliwattHours) *
                       1'000U) /
                      _totalMilliwattHours);

        const bool available =
            !charging &&
            _status.measurementFreshness ==
                BatteryMeasurementFreshness::Fresh &&
            _status.averagePowerMilliwatts >=
                _config->minimumTimeToEmptyPowerMilliwatts &&
            _status.sourceState != BatterySourceState::Absent &&
            _status.confidence != BatteryEstimateConfidence::Unavailable;
        if (available) {
            _status.timeToEmptyMinutes =
                detail::timeToEmptyMinutes(_status.remainingMilliwattHours,
                                           _status.averagePowerMilliwatts);
        } else {
            _status.timeToEmptyMinutes.reset();
        }
    }

    const BatteryConfig *_config = nullptr;
    BatteryStatus _status{};
    const BatteryProfile *_profile = nullptr;
    BatteryMeasurement _previousMeasurement{};
    uint64_t _chargeRemainder = 0;
    uint64_t _energyRemainder = 0;
    int64_t _averagePowerRemainder = 0;
    uint32_t _totalMilliampHours = 0;
    uint32_t _totalMilliwattHours = 0;
    uint32_t _initialRemainingMilliampHours = 0;
    uint32_t _initialRemainingMilliwattHours = 0;
    bool _hasPrevious = false;
    bool _initialized = false;
};

} // namespace Totem::BatteryMonitor::detail
