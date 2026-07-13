#pragma once

#include "AudioFft/Interfaces/PeakDetectorConfig.hpp"
#include "AudioFft/Interfaces/Types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace Totem::AudioFft::detail {

class PeakDetector {
  public:
    using Events = std::array<std::optional<PeakResult>, peakGroupCount>;

    void reset(const PeakDetectorConfig &config) {
        _config = config;
        for (auto &state : _states) {
            state.reset();
        }
    }

    [[nodiscard]] Events update(const FftResult &frame) {
        Events events{};
        for (const auto &groupConfig : _config.groups) {
            const auto index = peakGroupIndex(groupConfig.group);
            if (index >= _states.size()) {
                continue;
            }
            events[index] = _states[index].update(frame, groupConfig);
        }
        return events;
    }

    [[nodiscard]] PeakGroup indicatorGroup() const {
        return _config.indicatorGroup;
    }

  private:
    struct GroupState {
        void reset() {
            baseline = 0.0F;
            fluxBaseline = 0.0F;
            ambientFloor = 0.0F;
            baselineInitialized = false;
            fluxBaselineInitialized = false;
            ambientFloorInitialized = false;
            lastEnergy = 0.0F;
            lastPeakUs = 0;
            ratePerMinute = 0.0F;
            ibi.fill(0);
            ibiCount = 0;
            ibiHead = 0;
        }

        [[nodiscard]] std::optional<PeakResult>
        update(const FftResult &frame, const PeakGroupConfig &config) {
            const auto energy = _energy(frame, config.energyBands);
            const auto previousBaseline =
                baselineInitialized ? baseline : 0.0F;
            const auto previousAmbientFloor =
                ambientFloorInitialized ? ambientFloor : 0.0F;
            const bool refractoryOk =
                lastPeakUs == 0 ||
                frame.timestampUs >=
                    lastPeakUs +
                        (static_cast<uint64_t>(config.refractoryMs) * 1000ULL);
            const auto ambientThreshold = std::max(
                config.minEnergy,
                std::max(previousAmbientFloor * config.ambientSensitivity,
                         previousAmbientFloor + config.ambientEnergyMargin));
            const bool aboveNoise = energy >= ambientThreshold;
            const auto baselineThreshold =
                std::max(previousBaseline * config.sensitivity,
                         previousBaseline + config.onsetDelta);
            const bool aboveBaseline = energy > baselineThreshold;
            const auto flux = energy > lastEnergy ? energy - lastEnergy : 0.0F;
            const auto previousFluxBaseline =
                fluxBaselineInitialized ? fluxBaseline : 0.0F;
            const bool rising =
                flux >= config.onsetDelta &&
                flux > std::max(config.onsetDelta,
                                previousFluxBaseline * config.sensitivity);
            const bool candidate = aboveNoise && (aboveBaseline || rising);

            if (!candidate || !refractoryOk) {
                updateAmbientFloor(energy, config);
                updateBaseline(energy, config);
                updateFluxBaseline(flux, config);
            }

            if (candidate && refractoryOk) {
                registerPeak(frame.timestampUs, config);
                lastEnergy = energy;
                return PeakResult{
                    .group = config.group,
                    .bands = config.energyBands,
                    .frameSequence = frame.sequence,
                    .timestampUs = frame.timestampUs,
                    .energy = _eventEnergy(energy),
                    .ratePerMinute = ratePerMinute,
                };
            }

            lastEnergy = energy;
            return std::nullopt;
        }

        [[nodiscard]] static float _energy(const FftResult &frame,
                                           FftBandIndexRange bands) {
            float sum = 0.0F;
            for (uint8_t i = bands.lower; i <= bands.upper; ++i) {
                const auto value = frame.bands[i].weightedMagnitude;
                if (std::isfinite(value) && value > 0.0F) {
                    sum += value;
                }
            }
            return sum;
        }

        [[nodiscard]] static uint8_t _eventEnergy(float energy) {
            if (!std::isfinite(energy) || energy <= 0.0F) {
                return 0;
            }
            return static_cast<uint8_t>(
                std::clamp(std::lround(energy), 0L, 255L));
        }

        void updateBaseline(float energy, const PeakGroupConfig &config) {
            if (!baselineInitialized) {
                baseline = energy;
                baselineInitialized = true;
                return;
            }
            baseline = ((1.0F - config.baselineAlpha) * baseline) +
                       (config.baselineAlpha * energy);
        }

        void updateFluxBaseline(float flux, const PeakGroupConfig &config) {
            if (!fluxBaselineInitialized) {
                fluxBaseline = flux;
                fluxBaselineInitialized = true;
                return;
            }
            fluxBaseline = ((1.0F - config.baselineAlpha) * fluxBaseline) +
                           (config.baselineAlpha * flux);
        }

        void updateAmbientFloor(float energy, const PeakGroupConfig &config) {
            if (!ambientFloorInitialized) {
                ambientFloor = energy;
                ambientFloorInitialized = true;
                return;
            }
            if (energy < ambientFloor) {
                ambientFloor = energy;
                return;
            }
            ambientFloor = ((1.0F - config.ambientAlpha) * ambientFloor) +
                           (config.ambientAlpha * energy);
        }

        void registerPeak(uint64_t nowUs, const PeakGroupConfig &config) {
            if (lastPeakUs != 0 && nowUs > lastPeakUs) {
                pushIbi(static_cast<uint32_t>(nowUs - lastPeakUs), config);
                updateRate(config);
            }
            lastPeakUs = nowUs;
        }

        void pushIbi(uint32_t intervalUs, const PeakGroupConfig &config) {
            const auto maxIbi = static_cast<uint32_t>(
                60000000ULL /
                std::max<uint16_t>(1, config.minRatePerMinute));
            const auto minIbi = static_cast<uint32_t>(
                60000000ULL /
                std::max<uint16_t>(1, config.maxRatePerMinute));
            if (intervalUs < minIbi || intervalUs > maxIbi) {
                return;
            }

            const auto capacity = config.ibiHistorySize;
            ibi[ibiHead] = intervalUs;
            ibiHead = static_cast<uint8_t>((ibiHead + 1U) % capacity);
            if (ibiCount < capacity) {
                ++ibiCount;
            }
        }

        void updateRate(const PeakGroupConfig &config) {
            if (ibiCount < 2) {
                return;
            }

            std::array<uint32_t, 16> sorted{};
            for (uint8_t i = 0; i < ibiCount; ++i) {
                sorted[i] = ibi[i];
            }
            std::sort(sorted.begin(), sorted.begin() + ibiCount);
            const bool even = (ibiCount % 2U) == 0U;
            const auto intervalUs = even
                                        ? ((sorted[(ibiCount / 2U) - 1U] / 2U) +
                                           (sorted[ibiCount / 2U] / 2U))
                                        : sorted[ibiCount / 2U];
            if (intervalUs == 0) {
                return;
            }

            const auto calculatedRate =
                60.0F * 1000000.0F / static_cast<float>(intervalUs);
            ratePerMinute = std::clamp(
                calculatedRate, static_cast<float>(config.minRatePerMinute),
                static_cast<float>(config.maxRatePerMinute));
        }

        float baseline = 0.0F;
        float fluxBaseline = 0.0F;
        float ambientFloor = 0.0F;
        bool baselineInitialized = false;
        bool fluxBaselineInitialized = false;
        bool ambientFloorInitialized = false;
        float lastEnergy = 0.0F;
        uint64_t lastPeakUs = 0;
        float ratePerMinute = 0.0F;
        std::array<uint32_t, 16> ibi{};
        uint8_t ibiCount = 0;
        uint8_t ibiHead = 0;
    };

    PeakDetectorConfig _config{};
    std::array<GroupState, peakGroupCount> _states{};
};

} // namespace Totem::AudioFft::detail
