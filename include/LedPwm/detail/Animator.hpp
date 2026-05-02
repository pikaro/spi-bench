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
