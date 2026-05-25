#pragma once

#include "LedPwm/Interfaces/Types.hpp"
#include "StaticConfig/LedPwm.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <variant>

namespace Totem::LedPwm::detail {

class Animator {
    struct Slot {
        bool active = false;
        uint32_t startedMs = 0;
        Animation animation{};
    };

  public:
    ReturnCode setBase(Brightness brightness) {
        _base = brightness;
        _hasBase = true;
        _hasOutput = true;
        return OK();
    }

    void reset() {
        for (auto &slot : _slots) {
            slot.active = false;
        }
        _base = Brightness::off();
        _hasBase = false;
        _hasOutput = false;
    }

    ReturnCode start(Animation animation, uint32_t nowMs) {
        FAIL_IF_NOT(validate(animation), ERR(InvalidArgument),
                    "Invalid LED animation");

        auto &slot = slotFor(nowMs);
        slot = Slot{
            .active = true,
            .startedMs = nowMs,
            .animation = animation,
        };
        _hasOutput = true;
        return OK();
    }

    ReturnCode clearAnimations() {
        for (auto &slot : _slots) {
            slot.active = false;
        }
        _hasOutput = true;
        return OK();
    }

    [[nodiscard]] std::optional<Brightness> step(uint32_t nowMs) {
        if (!_hasOutput) {
            return std::nullopt;
        }

        auto output = _hasBase ? _base : Brightness::off();
        bool hasActiveAnimation = false;
        for (auto &slot : _slots) {
            if (!slot.active) {
                continue;
            }

            const auto elapsedMs = nowMs - slot.startedMs;
            auto value = evaluate(slot.animation, elapsedMs);
            if (!value.has_value()) {
                slot.active = false;
                continue;
            }

            hasActiveAnimation = true;
            output = max(output, *value);
        }

        _hasOutput = hasActiveAnimation;
        return output;
    }

  private:
    static constexpr bool validate(const Animation &animation) {
        return std::visit([](const auto &payload) { return payload.validate(); },
                          animation);
    }

    Slot &slotFor(uint32_t nowMs) {
        for (auto &slot : _slots) {
            if (!slot.active) {
                return slot;
            }
        }

        auto *lowest = &_slots[0];
        auto lowestValue = currentValue(*lowest, nowMs);
        for (auto &slot : _slots) {
            const auto value = currentValue(slot, nowMs);
            if (value.value.value < lowestValue.value.value) {
                lowest = &slot;
                lowestValue = value;
            }
        }
        return *lowest;
    }

    static Brightness currentValue(const Slot &slot, uint32_t nowMs) {
        if (!slot.active) {
            return Brightness::off();
        }
        return evaluate(slot.animation, nowMs - slot.startedMs)
            .value_or(Brightness::off());
    }

    static std::optional<Brightness> evaluate(const Animation &animation,
                                              uint32_t elapsedMs) {
        return std::visit(
            [elapsedMs](const auto &payload) -> std::optional<Brightness> {
                return evaluate(payload, elapsedMs);
            },
            animation);
    }

    static std::optional<Brightness> evaluate(const Pulse &pulse,
                                              uint32_t elapsedMs) {
        if (!pulse.validate() || elapsedMs >= pulse.durationMs()) {
            return std::nullopt;
        }

        if (elapsedMs < pulse.riseMs) {
            return scale(pulse.peak, elapsedMs, pulse.riseMs, pulse.curve);
        }

        elapsedMs -= pulse.riseMs;
        if (elapsedMs < pulse.holdMs) {
            return pulse.peak;
        }

        elapsedMs -= pulse.holdMs;
        if (pulse.fallMs == 0) {
            return std::nullopt;
        }
        return scale(pulse.peak, pulse.fallMs - elapsedMs, pulse.fallMs,
                     pulse.curve);
    }

    static std::optional<Brightness> evaluate(const Glitter &glitter,
                                              uint32_t elapsedMs) {
        if (!glitter.validate()) {
            return std::nullopt;
        }

        const auto segment = elapsedMs / glitter.stepMs;
        const auto offset = elapsedMs % glitter.stepMs;
        auto value =
            blend(randomGlimmer(glitter, segment),
                  randomGlimmer(glitter, segment + 1U), offset,
                  glitter.stepMs, Curve::SmoothStep);

        if (glitter.sparkleMs > 0 && offset < glitter.sparkleMs) {
            const auto chance =
                static_cast<uint8_t>(mix(glitter.seed, segment) >> 24U);
            if (chance < glitter.sparkleChance) {
                value =
                    max(value, scale(glitter.sparklePeak,
                                     glitter.sparkleMs - offset,
                                     glitter.sparkleMs, Curve::SmoothStep));
            }
        }

        return value;
    }

    static Brightness scale(Brightness brightness, uint32_t numerator,
                            uint32_t denominator, Curve curve) {
        if (denominator == 0) {
            return brightness;
        }

        const auto fraction = applyCurve(numerator, denominator, curve);
        const auto value = static_cast<uint16_t>(
            (static_cast<uint64_t>(brightness.value.value) * fraction) /
            NormalizedValue::max);
        return Brightness::fromRaw(value);
    }

    static Brightness blend(Brightness from, Brightness to,
                            uint32_t numerator, uint32_t denominator,
                            Curve curve) {
        if (denominator == 0 || numerator >= denominator) {
            return to;
        }

        const auto fraction = applyCurve(numerator, denominator, curve);
        const auto fromRaw = from.value.value;
        const auto toRaw = to.value.value;
        const auto raw =
            fromRaw <= toRaw
                ? fromRaw + static_cast<uint16_t>(
                                (static_cast<uint32_t>(toRaw - fromRaw) *
                                 fraction) /
                                NormalizedValue::max)
                : fromRaw - static_cast<uint16_t>(
                                (static_cast<uint32_t>(fromRaw - toRaw) *
                                 fraction) /
                                NormalizedValue::max);
        return Brightness::fromRaw(raw);
    }

    static Brightness randomGlimmer(const Glitter &glitter,
                                    uint32_t segment) {
        const auto range = static_cast<uint16_t>(
            glitter.glimmerPeak.value.value - glitter.base.value.value);
        const auto rnd = static_cast<uint16_t>(
            (mix(glitter.seed ^ 0xB17DU, segment) >> 16U) & 0xFFFFU);
        const auto raw = static_cast<uint16_t>(
            glitter.base.value.value +
            ((static_cast<uint32_t>(range) * rnd) / NormalizedValue::max));
        return Brightness::fromRaw(raw);
    }

    static uint32_t mix(uint32_t seed, uint32_t value) {
        auto x = seed ^ (value + 0x9E3779B9UL + (seed << 6U) + (seed >> 2U));
        x ^= x >> 16U;
        x *= 0x7FEB352DUL;
        x ^= x >> 15U;
        x *= 0x846CA68BUL;
        x ^= x >> 16U;
        return x;
    }

    static uint16_t applyCurve(uint32_t numerator, uint32_t denominator,
                               Curve curve) {
        if (numerator >= denominator) {
            return NormalizedValue::max;
        }

        const auto t = static_cast<uint32_t>(
            (static_cast<uint64_t>(numerator) * NormalizedValue::max) /
            denominator);

        switch (curve) {
        case Curve::SmoothStep:
            return smoothStep(static_cast<uint16_t>(t));
        case Curve::Linear:
        default:
            return static_cast<uint16_t>(t);
        }
    }

    static uint16_t smoothStep(uint16_t t) {
        constexpr uint64_t max = NormalizedValue::max;
        const uint64_t x = t;
        const uint64_t x2 = x * x;
        const uint64_t x3 = x2 * x;
        return static_cast<uint16_t>(((3ULL * max * x2) - (2ULL * x3)) /
                                     (max * max));
    }

    static constexpr Brightness max(Brightness lhs, Brightness rhs) {
        return lhs.value.value >= rhs.value.value ? lhs : rhs;
    }

    std::array<Slot, LedPwmConfig::animationSlots> _slots{};
    Brightness _base{Brightness::off()};
    bool _hasBase = false;
    bool _hasOutput = false;
};

} // namespace Totem::LedPwm::detail
