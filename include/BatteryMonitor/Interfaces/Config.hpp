#pragma once

#include "BatteryMonitor/Interfaces/Types.hpp"
#include "StaticConfig/FileSystem.hpp"
#include "StaticConfig/MetricsLimits.hpp"
#include <cstdint>
#include <limits>
#include <string_view>

namespace Totem::BatteryMonitor {

struct BatteryConfig {
    BatteryChemistry chemistry = BatteryChemistry::LiIon4V2;
    uint8_t seriesCells = 7;
    uint8_t parallelCells = 4;
    uint32_t nominalPackCapacityMilliampHours = 10'000;
    uint32_t nominalCellMillivolts = 3'700;

    uint32_t practicalMinCellMillivolts = 3'000;
    uint32_t practicalMaxCellMillivolts = 4'200;
    uint32_t absoluteMinCellMillivolts = 2'500;
    uint32_t absoluteMaxCellMillivolts = 4'300;
    uint32_t fullQualificationMinCellMillivolts = 4'000;

    uint32_t sampleGapToleranceMs = 2'000;
    uint32_t persistenceIntervalMs = 60'000;
    uint32_t averagePowerWindowMs = 300'000;
    uint32_t cutoffDwellMs = 5'000;
    uint32_t loadRemovedDwellMs = 60'000;
    uint32_t maximumCalibrationDurationMs = 129'600'000; // 36 hours

    uint32_t currentDeadbandMicroamps = 5'000;
    uint32_t minimumDischargeMicroamps = 25'000;
    uint32_t cutoffCurrentMicroamps = 10'000;
    uint32_t minimumTimeToEmptyPowerMilliwatts = 100;
    uint32_t disconnectedVoltageMillivolts = 1'000;

    uint32_t packId = 1;
    uint32_t calibrationLoadMilliohms = 50'000;
    uint32_t maximumJournalRecords = 16'384;
    uint8_t curveBuildRecordsPerWork = 4;

    // Both strings are copied into component-owned fixed buffers during
    // begin().
    const char *profilePath = "/battery.bin";
    const char *metricsGroupName = "battery";

    [[nodiscard]] constexpr uint32_t
    packVoltage(uint32_t cellMillivolts) const {
        return static_cast<uint32_t>(seriesCells) * cellMillivolts;
    }

    [[nodiscard]] constexpr uint32_t practicalMinPackMillivolts() const {
        return packVoltage(practicalMinCellMillivolts);
    }

    [[nodiscard]] constexpr uint32_t practicalMaxPackMillivolts() const {
        return packVoltage(practicalMaxCellMillivolts);
    }

    [[nodiscard]] constexpr uint32_t absoluteMinPackMillivolts() const {
        return packVoltage(absoluteMinCellMillivolts);
    }

    [[nodiscard]] constexpr uint32_t absoluteMaxPackMillivolts() const {
        return packVoltage(absoluteMaxCellMillivolts);
    }

    [[nodiscard]] constexpr uint32_t
    fullQualificationMinPackMillivolts() const {
        return packVoltage(fullQualificationMinCellMillivolts);
    }

    [[nodiscard]] constexpr uint32_t nominalPackEnergyMilliwattHours() const {
        const uint64_t nominalPackMillivolts =
            static_cast<uint64_t>(seriesCells) * nominalCellMillivolts;
        return static_cast<uint32_t>(
            (static_cast<uint64_t>(nominalPackCapacityMilliampHours) *
             nominalPackMillivolts) /
            1'000U);
    }

    [[nodiscard]] constexpr uint32_t maximumIntervalRecords() const {
        return persistenceIntervalMs == 0
                   ? 0
                   : maximumCalibrationDurationMs / persistenceIntervalMs +
                         (maximumCalibrationDurationMs %
                                      persistenceIntervalMs !=
                                  0
                              ? 1U
                              : 0U) +
                         1U; // final partial interval
    }

    [[nodiscard]] constexpr uint32_t maximumSessionRecords() const {
        return 1U + maximumIntervalRecords() +
               static_cast<uint32_t>(batteryProfilePointCount) + 1U;
    }

    [[nodiscard]] constexpr bool validate() const {
        if (chemistry != BatteryChemistry::LiIon4V2 || seriesCells == 0 ||
            seriesCells > 32 || parallelCells == 0 || parallelCells > 32 ||
            nominalPackCapacityMilliampHours == 0 ||
            nominalCellMillivolts == 0 || sampleGapToleranceMs == 0 ||
            persistenceIntervalMs == 0 || averagePowerWindowMs == 0 ||
            cutoffDwellMs == 0 || loadRemovedDwellMs == 0 ||
            maximumCalibrationDurationMs == 0 ||
            sampleGapToleranceMs >
                static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
            persistenceIntervalMs >
                static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
            averagePowerWindowMs >
                static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
            cutoffDwellMs >
                static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
            loadRemovedDwellMs >
                static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
            maximumCalibrationDurationMs >
                static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
            currentDeadbandMicroamps == 0 ||
            minimumDischargeMicroamps <= currentDeadbandMicroamps ||
            cutoffCurrentMicroamps > minimumDischargeMicroamps ||
            minimumDischargeMicroamps >
                static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
            minimumTimeToEmptyPowerMilliwatts == 0 ||
            disconnectedVoltageMillivolts >= absoluteMinPackMillivolts() ||
            packId == 0 || calibrationLoadMilliohms == 0 ||
            maximumJournalRecords == 0 ||
            maximumJournalRecords < maximumSessionRecords() ||
            curveBuildRecordsPerWork == 0 || profilePath == nullptr ||
            profilePath[0] != '/' || metricsGroupName == nullptr ||
            metricsGroupName[0] == '\0') {
            return false;
        }

        if (!(absoluteMinCellMillivolts < practicalMinCellMillivolts &&
              practicalMinCellMillivolts < practicalMaxCellMillivolts &&
              practicalMaxCellMillivolts <= absoluteMaxCellMillivolts &&
              absoluteMaxCellMillivolts <= 5'000 &&
              nominalCellMillivolts >= absoluteMinCellMillivolts &&
              nominalCellMillivolts <= absoluteMaxCellMillivolts &&
              fullQualificationMinCellMillivolts >=
                  practicalMinCellMillivolts &&
              fullQualificationMinCellMillivolts <=
                  practicalMaxCellMillivolts)) {
            return false;
        }

        const std::string_view path{profilePath};
        const std::string_view metricsName{metricsGroupName};
        if (path.size() >= FileSystemConfig::maxPathLength ||
            metricsName.size() > MetricLimits::maxMetricGroupNameLength) {
            return false;
        }

        const uint64_t nominalEnergy =
            (static_cast<uint64_t>(nominalPackCapacityMilliampHours) *
             seriesCells * nominalCellMillivolts) /
            1'000U;
        return nominalEnergy > 0 &&
               nominalEnergy <= std::numeric_limits<uint32_t>::max();
    }
};

inline constexpr BatteryConfig default7s4pConfig{};
static_assert(default7s4pConfig.validate());
static_assert(default7s4pConfig.practicalMinPackMillivolts() == 21'000);
static_assert(default7s4pConfig.practicalMaxPackMillivolts() == 29'400);
static_assert(default7s4pConfig.nominalPackEnergyMilliwattHours() == 259'000);
static_assert(default7s4pConfig.maximumIntervalRecords() == 2'161);
static_assert(default7s4pConfig.maximumSessionRecords() == 2'264);

} // namespace Totem::BatteryMonitor
