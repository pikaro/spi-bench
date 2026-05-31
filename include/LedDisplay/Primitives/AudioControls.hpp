#pragma once

#include "Audio/Interfaces/Types.hpp"
#include "Audio/Interfaces/Wire.hpp"
#include "LedDisplay/Renderers/GenericRenderer.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

namespace Totem::LedDisplay::Primitives {

struct AudioControls {
    uint8_t bass = 0;
    uint8_t mid = 0;
    uint8_t high = 0;
    uint8_t energy = 0;
    uint8_t bassAttack = 0;
    uint8_t midAttack = 0;
    uint8_t highAttack = 0;
    bool hasInput = false;
};

class AudioControlSmoother {
  public:
    [[nodiscard]] AudioControls update(const Totem::Audio::FftFrame &frame,
                                       bool hasFrame,
                                       const Totem::Audio::PeakEvent &peak,
                                       bool hasPeak) {
        decayAttacks();
        if (hasFrame) {
            const auto rawBass = average2(normalize(frame.subBass),
                                          normalize(frame.bass));
            const auto rawMid = average3(normalize(frame.lowMid),
                                         normalize(frame.mid),
                                         normalize(frame.highMid));
            const auto rawHigh = average3(normalize(frame.presence),
                                          normalize(frame.brilliance),
                                          normalize(frame.air));

            if (!_controls.hasInput) {
                _controls.bass = rawBass;
                _controls.mid = rawMid;
                _controls.high = rawHigh;
            } else {
                _controls.bass = smooth(_controls.bass, rawBass);
                _controls.mid = smooth(_controls.mid, rawMid);
                _controls.high = smooth(_controls.high, rawHigh);
            }
            _controls.energy = average3(_controls.bass, _controls.mid,
                                        _controls.high);
            _controls.hasInput = true;
        }

        if (hasPeak) {
            applyPeak(peak);
        }
        return _controls;
    }

    [[nodiscard]] AudioControls current() const { return _controls; }

  private:
    static constexpr uint8_t attackAlpha = 24;
    static constexpr uint8_t releaseAlpha = 8;
    static constexpr uint8_t inputDeadband = 2;
    static constexpr uint8_t attackDecay = 5;
    static constexpr uint16_t maxWireBandByteValue = 255;
    static constexpr uint8_t wireBandHighByteShift = 8;

    [[nodiscard]] static uint8_t normalize(uint16_t raw) {
        if (raw <= maxWireBandByteValue) {
            return static_cast<uint8_t>(raw);
        }
        return static_cast<uint8_t>(raw >> wireBandHighByteShift);
    }

    [[nodiscard]] static constexpr uint8_t average2(uint8_t a, uint8_t b) {
        return static_cast<uint8_t>((static_cast<uint16_t>(a) + b) / 2U);
    }

    [[nodiscard]] static constexpr uint8_t average3(uint8_t a, uint8_t b,
                                                    uint8_t c) {
        return static_cast<uint8_t>((static_cast<uint16_t>(a) + b + c) / 3U);
    }

    [[nodiscard]] static uint8_t smooth(uint8_t current, uint8_t target) {
        const auto diff = current > target
                              ? static_cast<uint8_t>(current - target)
                              : static_cast<uint8_t>(target - current);
        if (diff <= inputDeadband) {
            return current;
        }
        const auto alpha = target > current ? attackAlpha : releaseAlpha;
        const auto retained = Renderers::GenericRenderer::scale8(
            current, static_cast<uint8_t>(255U - alpha));
        const auto incoming =
            Renderers::GenericRenderer::scale8(target, alpha);
        return Renderers::GenericRenderer::qadd8(retained, incoming);
    }

    static void decay(uint8_t &value) {
        value = value > attackDecay ? static_cast<uint8_t>(value - attackDecay)
                                    : 0;
    }

    void decayAttacks() {
        decay(_controls.bassAttack);
        decay(_controls.midAttack);
        decay(_controls.highAttack);
    }

    void applyPeak(const Totem::Audio::PeakEvent &peak) {
        const auto groupIndex = Totem::Audio::peakGroupIndex(peak.group);
        if (groupIndex >= _lastPeakFrame.size()) {
            return;
        }
        if (_lastPeakFrame[groupIndex] == peak.frameSequence) {
            return;
        }
        _lastPeakFrame[groupIndex] = peak.frameSequence;
        switch (peak.group) {
        case Totem::Audio::PeakGroup::Bass:
            _controls.bassAttack =
                std::max(_controls.bassAttack, peak.energy);
            break;
        case Totem::Audio::PeakGroup::Mid:
            _controls.midAttack = std::max(_controls.midAttack, peak.energy);
            break;
        case Totem::Audio::PeakGroup::High:
            _controls.highAttack =
                std::max(_controls.highAttack, peak.energy);
            break;
        default:
            break;
        }
        _controls.energy = std::max(_controls.energy, peak.energy);
        _controls.hasInput = true;
    }

    AudioControls _controls{};
    std::array<uint32_t, Totem::Audio::peakGroupCount> _lastPeakFrame{};
};

} // namespace Totem::LedDisplay::Primitives
