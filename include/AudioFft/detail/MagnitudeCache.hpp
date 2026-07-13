#pragma once

#include "AudioFft/Interfaces/Types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Totem::AudioFft::detail {

class IMagnitudeCache {
  public:
    virtual ~IMagnitudeCache() = default;
    virtual void reset(const FftMagnitudeCacheConfig &config) = 0;
    virtual void update(FftResult &frame) = 0;
};

class MagnitudeCacheBase : public IMagnitudeCache {
  public:
    void reset(const FftMagnitudeCacheConfig &config) override {
        _config = config;
        _scaled.fill(0);
        _resetImpl();
    }

  protected:
    struct Limit {
        float floor = 0.0F;
        float peak = 0.0F;
    };

    [[nodiscard]] static Limit
    updateLimit(Limit limit, float value, const FftMagnitudeCacheConfig &config,
                bool initialized) {
        if (!initialized) {
            return Limit{
                .floor = 0.0F,
                .peak = std::max(value, config.minimumRange),
            };
        }

        if (value < limit.floor) {
            limit.floor = value;
        } else {
            limit.floor += (value - limit.floor) * config.floorAdaptAlpha;
        }

        if (value > limit.peak) {
            limit.peak = value;
        } else {
            limit.peak += (value - limit.peak) * config.peakAdaptAlpha;
        }

        if (limit.peak < limit.floor + config.minimumRange) {
            limit.peak = limit.floor + config.minimumRange;
        }

        const auto highestUsefulFloor =
            value > config.minimumRange ? value - config.minimumRange : 0.0F;
        limit.floor = std::min(limit.floor, highestUsefulFloor);

        return limit;
    }

    [[nodiscard]] static uint8_t scale(float value, Limit limit) {
        const auto range = limit.peak - limit.floor;
        if (range <= 0.0F) {
            return 0;
        }
        const auto normalized =
            std::clamp((value - limit.floor) / range, 0.0F, 1.0F);
        return static_cast<uint8_t>(std::lround(normalized * 255.0F));
    }

    [[nodiscard]] uint8_t applyGate(uint8_t value) const {
        const auto gate = _config.scaledNoiseGate;
        if (value <= gate) {
            return 0;
        }
        return static_cast<uint8_t>(
            ((static_cast<uint16_t>(value) - gate) * 255U) / (255U - gate));
    }

    uint8_t smooth(size_t index, uint8_t target) {
        const auto current = _scaled[index];
        const auto alpha = target > current ? _config.scaledAttackAlpha
                                            : _config.scaledReleaseAlpha;
        const auto next =
            static_cast<float>(current) +
            ((static_cast<float>(target) - static_cast<float>(current)) *
             alpha);
        _scaled[index] =
            static_cast<uint8_t>(std::clamp(std::lround(next), 0L, 255L));
        return _scaled[index];
    }

    [[nodiscard]] static float finiteOrZero(float value) {
        return std::isfinite(value) && value > 0.0F ? value : 0.0F;
    }

    virtual void _resetImpl() = 0;

    FftMagnitudeCacheConfig _config{};

  private:
    std::array<uint8_t, fftBandCount> _scaled{};
};

class PerBandMagnitudeCache : public MagnitudeCacheBase {
  public:
    void update(FftResult &frame) override {
        for (size_t i = 0; i < fftBandCount; ++i) {
            auto &band = frame.bands[i];
            const auto value = finiteOrZero(band.weightedMagnitude);
            _limits[i] =
                updateLimit(_limits[i], value, _config, _initialized[i]);
            _initialized[i] = true;
            band.floor = _limits[i].floor;
            band.peak = _limits[i].peak;
            band.scaled = smooth(i, applyGate(scale(value, _limits[i])));
        }
    }

  private:
    void _resetImpl() override {
        _initialized.fill(false);
        for (auto &limit : _limits) {
            limit = {};
        }
    }

    std::array<Limit, fftBandCount> _limits{};
    std::array<bool, fftBandCount> _initialized{};
};

class TotalEnergyMagnitudeCache : public MagnitudeCacheBase {
  public:
    void update(FftResult &frame) override {
        const auto total = _totalEnergy(frame);
        _limit = updateLimit(_limit, total, _config, _initialized);
        _initialized = true;

        const auto perBandFloor =
            _limit.floor / static_cast<float>(fftBandCount);
        const auto sharedRange =
            std::max(_limit.peak - _limit.floor, _config.minimumRange);
        for (size_t i = 0; i < fftBandCount; ++i) {
            auto &band = frame.bands[i];
            const auto value = std::max(
                0.0F, finiteOrZero(band.weightedMagnitude) - perBandFloor);
            const auto normalized = std::clamp(value / sharedRange, 0.0F, 1.0F);
            const auto scaled =
                static_cast<uint8_t>(std::lround(normalized * 255.0F));
            band.floor = perBandFloor;
            band.peak = perBandFloor + sharedRange;
            band.scaled = smooth(i, applyGate(scaled));
        }
    }

  private:
    void _resetImpl() override {
        _limit = {};
        _initialized = false;
    }

    [[nodiscard]] static float _totalEnergy(const FftResult &frame) {
        float total = 0.0F;
        for (const auto &band : frame.bands) {
            total += finiteOrZero(band.weightedMagnitude);
        }
        return total;
    }

    Limit _limit{};
    bool _initialized = false;
};

class MagnitudeCache {
  public:
    void reset(const FftMagnitudeCacheConfig &config) {
        _active = _select(config.mode);
        _active->reset(config);
    }

    void update(FftResult &frame) { _active->update(frame); }

  private:
    IMagnitudeCache *_select(FftMagnitudeCacheMode mode) {
        switch (mode) {
        case FftMagnitudeCacheMode::TotalEnergyAdaptive:
            return &_totalEnergy;
        case FftMagnitudeCacheMode::PerBandAdaptive:
        default:
            return &_perBand;
        }
    }

    PerBandMagnitudeCache _perBand{};
    TotalEnergyMagnitudeCache _totalEnergy{};
    IMagnitudeCache *_active = &_perBand;
};

} // namespace Totem::AudioFft::detail
