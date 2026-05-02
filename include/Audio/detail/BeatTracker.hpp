#pragma once

#include "Audio/Interfaces/BeatTrackerConfig.hpp"
#include "Audio/Interfaces/Types.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace Totem::Audio::detail {

class BeatTracker {
  public:
    void reset(const BeatTrackerConfig &config) {
        _config = config;
        _baseline = 0.0F;
        _baselineInitialized = false;
        _lastEnergy = 0;
        _lastBeatUs = 0;
        _bpm = 0.0F;
        _ibi.fill(0);
        _ibiCount = 0;
        _ibiHead = 0;
    }

    [[nodiscard]] std::optional<BeatEvent> update(const FftFrame &frame) {
        const auto energy = _energy(frame);
        const auto previousBaseline =
            _baselineInitialized ? _baseline : static_cast<float>(energy);
        const bool refractoryOk =
            _lastBeatUs == 0 ||
            frame.timestampUs >=
                _lastBeatUs +
                    (static_cast<uint64_t>(_config.refractoryMs) * 1000ULL);
        const bool aboveNoise = energy >= _config.minEnergy;
        const bool aboveBaseline =
            static_cast<float>(energy) > previousBaseline * _config.sensitivity;
        const bool rising =
            energy > static_cast<uint16_t>(_lastEnergy + _config.onsetDelta);

        _updateBaseline(energy);

        if (aboveNoise && aboveBaseline && rising && refractoryOk) {
            _registerBeat(frame.timestampUs);
            _lastEnergy = energy;
            return BeatEvent{
                .kind = BeatEventKind::Detected,
                .frameSequence = frame.sequence,
                .timestampUs = frame.timestampUs,
                .energy = static_cast<uint8_t>(std::min<uint16_t>(energy, 255)),
                .bpm = _bpm,
            };
        }

        _lastEnergy = energy;
        return std::nullopt;
    }

    [[nodiscard]] float bpm() const { return _bpm; }

  private:
    [[nodiscard]] uint16_t _energy(const FftFrame &frame) const {
        uint16_t sum = 0;
        const auto lower = _config.energyBands.lower;
        const auto upper = _config.energyBands.upper;
        for (uint8_t i = lower; i <= upper; ++i) {
            sum = static_cast<uint16_t>(sum + frame.bands[i].scaled);
        }
        return static_cast<uint16_t>(sum / (upper - lower + 1U));
    }

    void _updateBaseline(uint16_t energy) {
        if (!_baselineInitialized) {
            _baseline = static_cast<float>(energy);
            _baselineInitialized = true;
            return;
        }
        _baseline = ((1.0F - _config.baselineAlpha) * _baseline) +
                    (_config.baselineAlpha * static_cast<float>(energy));
    }

    void _registerBeat(uint64_t nowUs) {
        if (_lastBeatUs != 0 && nowUs > _lastBeatUs) {
            _pushIbi(static_cast<uint32_t>(nowUs - _lastBeatUs));
            _updateBpm();
        }
        _lastBeatUs = nowUs;
    }

    void _pushIbi(uint32_t ibi) {
        const auto maxIbi = static_cast<uint32_t>(
            60000000ULL / std::max<uint16_t>(1, _config.minBpm));
        const auto minIbi = static_cast<uint32_t>(
            60000000ULL / std::max<uint16_t>(1, _config.maxBpm));
        if (ibi < minIbi || ibi > maxIbi) {
            return;
        }

        const auto capacity = _config.ibiHistorySize;
        _ibi[_ibiHead] = ibi;
        _ibiHead = static_cast<uint8_t>((_ibiHead + 1U) % capacity);
        if (_ibiCount < capacity) {
            ++_ibiCount;
        }
    }

    void _updateBpm() {
        if (_ibiCount < 2) {
            return;
        }

        std::array<uint32_t, 16> sorted{};
        for (uint8_t i = 0; i < _ibiCount; ++i) {
            sorted[i] = _ibi[i];
        }
        std::sort(sorted.begin(), sorted.begin() + _ibiCount);
        const bool even = (_ibiCount % 2U) == 0U;
        const auto ibi = even ? ((sorted[(_ibiCount / 2U) - 1U] / 2U) +
                                 (sorted[_ibiCount / 2U] / 2U))
                              : sorted[_ibiCount / 2U];
        if (ibi == 0) {
            return;
        }

        const auto bpm = 60.0F * 1000000.0F / static_cast<float>(ibi);
        _bpm = std::clamp(bpm, static_cast<float>(_config.minBpm),
                          static_cast<float>(_config.maxBpm));
    }

    BeatTrackerConfig _config{};
    float _baseline = 0.0F;
    bool _baselineInitialized = false;
    uint16_t _lastEnergy = 0;
    uint64_t _lastBeatUs = 0;
    float _bpm = 0.0F;
    std::array<uint32_t, 16> _ibi{};
    uint8_t _ibiCount = 0;
    uint8_t _ibiHead = 0;
};

} // namespace Totem::Audio::detail
