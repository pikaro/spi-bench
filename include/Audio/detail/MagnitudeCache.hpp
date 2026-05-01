#pragma once

#include "Audio/Interfaces/Types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Totem::Audio::detail {

class MagnitudeCache {
  public:
    void reset(const FftMagnitudeCacheConfig &config) {
        _config = config;
        _initialized.fill(false);
        for (auto &limit : _limits) {
            limit = {};
        }
    }

    void update(FftFrame &frame) {
        for (size_t i = 0; i < fftBandCount; ++i) {
            auto &band = frame.bands[i];
            const auto limits = _updateLimit(i, band.weightedMagnitude);
            band.floor = limits.floor;
            band.peak = limits.peak;
            band.scaled = _scale(band.weightedMagnitude, limits);
        }
    }

  private:
    struct Limit {
        float floor = 0.0F;
        float peak = 0.0F;
    };

    Limit _updateLimit(size_t index, float value) {
        auto &limit = _limits[index];
        if (!_initialized[index]) {
            limit.floor = value;
            limit.peak = value + _config.minimumRange;
            _initialized[index] = true;
            return limit;
        }

        if (value < limit.floor) {
            limit.floor = value;
        } else {
            limit.floor += (value - limit.floor) * _config.floorAdaptAlpha;
        }

        if (value > limit.peak) {
            limit.peak = value;
        } else {
            limit.peak += (value - limit.peak) * _config.peakAdaptAlpha;
        }

        if (limit.peak < limit.floor + _config.minimumRange) {
            limit.peak = limit.floor + _config.minimumRange;
        }

        return limit;
    }

    [[nodiscard]] static uint8_t _scale(float value, Limit limit) {
        const auto range = limit.peak - limit.floor;
        if (range <= 0.0F) {
            return 0;
        }
        const auto normalized =
            std::clamp((value - limit.floor) / range, 0.0F, 1.0F);
        return static_cast<uint8_t>(std::lround(normalized * 255.0F));
    }

    FftMagnitudeCacheConfig _config{};
    std::array<Limit, fftBandCount> _limits{};
    std::array<bool, fftBandCount> _initialized{};
};

} // namespace Totem::Audio::detail
