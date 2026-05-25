#pragma once

#include "LedPwm/detail/Types.hpp"
#include "Macros/internal/Markers.hpp"
#include "Types/Error.hpp"
#include <cstdint>
#include <limits>
#include <type_traits>
#include <variant>

namespace Totem::LedPwm {

using LedContext = detail::LedContext;

struct NormalizedValue {
    static constexpr uint16_t max = std::numeric_limits<uint16_t>::max();

    uint16_t value = 0;

    static constexpr NormalizedValue off() { return NormalizedValue{}; }
    static constexpr NormalizedValue full() {
        return NormalizedValue{.value = max};
    }
    static constexpr NormalizedValue fromPercent(float percent) {
        if (percent < 0.0F) {
            percent = 0.0F;
        } else if (percent > 100.0F) {
            percent = 100.0F;
        }
        return NormalizedValue{
            .value = static_cast<uint16_t>((percent / 100.0F) * max)};
    }
    static constexpr NormalizedValue fromRaw(uint16_t value) {
        return NormalizedValue{.value = value};
    }

    [[nodiscard]] constexpr uint32_t scaledToUint32() const {
        return static_cast<uint32_t>((static_cast<uint64_t>(value) *
                                      std::numeric_limits<uint32_t>::max()) /
                                     max);
    }
};

struct Duty {
    NormalizedValue value{};

    static constexpr Duty off() {
        return Duty{.value = NormalizedValue::off()};
    }
    static constexpr Duty full() {
        return Duty{.value = NormalizedValue::full()};
    }
    static constexpr Duty fromPercent(float percent) {
        return Duty{.value = NormalizedValue::fromPercent(percent)};
    }
    static constexpr Duty fromRaw(uint16_t value) {
        return Duty{.value = NormalizedValue::fromRaw(value)};
    }

    [[nodiscard]] constexpr uint32_t scaledToUint32() const {
        return value.scaledToUint32();
    }
};

struct Brightness {
    NormalizedValue value{};

    static constexpr Brightness off() {
        return Brightness{.value = NormalizedValue::off()};
    }
    static constexpr Brightness full() {
        return Brightness{.value = NormalizedValue::full()};
    }
    static constexpr Brightness fromPercent(float percent) {
        return Brightness{.value = NormalizedValue::fromPercent(percent)};
    }
    static constexpr Brightness fromRaw(uint16_t value) {
        return Brightness{.value = NormalizedValue::fromRaw(value)};
    }

    [[nodiscard]] constexpr uint32_t scaledToUint32() const {
        return value.scaledToUint32();
    }
};

struct SetDuty {
    Duty duty{};

    [[nodiscard]] constexpr bool validate() const { return true; }
};

struct SetBrightness {
    Brightness brightness{};

    [[nodiscard]] constexpr bool validate() const { return true; }
};

enum class Curve : uint8_t {
    Linear = 0,
    SmoothStep,
};

struct WIRE_MSG Pulse {
    Brightness peak{Brightness::full()};

    uint32_t riseMs = 50;
    uint32_t holdMs = 0;
    uint32_t fallMs = 150;

    Curve curve = Curve::SmoothStep;

    [[nodiscard]] constexpr uint32_t durationMs() const {
        return riseMs + holdMs + fallMs;
    }

    [[nodiscard]] constexpr bool validate() const {
        if (riseMs > std::numeric_limits<uint32_t>::max() - holdMs) {
            return false;
        }
        if ((riseMs + holdMs) > std::numeric_limits<uint32_t>::max() - fallMs) {
            return false;
        }
        return durationMs() > 0;
    }
};

struct WIRE_MSG Glitter {
    Brightness base{Brightness::fromPercent(2.0F)};
    Brightness glimmerPeak{Brightness::fromPercent(18.0F)};
    Brightness sparklePeak{Brightness::fromPercent(65.0F)};

    uint16_t stepMs = 120;
    uint16_t sparkleMs = 28;
    uint8_t sparkleChance = 36;
    uint16_t seed = 1;

    [[nodiscard]] constexpr bool validate() const {
        return stepMs > 0 && sparkleMs <= stepMs &&
               base.value.value <= glimmerPeak.value.value &&
               glimmerPeak.value.value <= sparklePeak.value.value;
    }
};

using Animation = std::variant<Pulse, Glitter>;

struct StartAnimation {
    Animation animation{Pulse{}};

    [[nodiscard]] constexpr bool validate() const {
        return std::visit(
            [](const auto &payload) { return payload.validate(); }, animation);
    }
};

struct ClearAnimations {
    [[nodiscard]] constexpr bool validate() const { return true; }
};

struct LedCommand {
    using Payload =
        std::variant<SetDuty, SetBrightness, StartAnimation, ClearAnimations>;

    Payload payload{SetDuty{}};

    static constexpr LedCommand setDuty(Duty duty) {
        return LedCommand{.payload = SetDuty{.duty = duty}};
    }

    static constexpr LedCommand setBrightness(Brightness brightness) {
        return LedCommand{.payload = SetBrightness{.brightness = brightness}};
    }

    static constexpr LedCommand startAnimation(Animation animation) {
        return LedCommand{.payload = StartAnimation{.animation = animation}};
    }

    static constexpr LedCommand pulse(Pulse pulse) {
        return startAnimation(Animation{pulse});
    }

    static constexpr LedCommand clearAnimations() {
        return LedCommand{.payload = ClearAnimations{}};
    }

    ReturnCode run(LedContext ctx) const {
        return ctx.command(static_cast<void *>(&ctx),
                           static_cast<const void *>(this));
    }
};

static_assert(std::is_trivially_copyable_v<NormalizedValue>,
              "NormalizedValue must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<Duty>,
              "Duty must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<Brightness>,
              "Brightness must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<SetDuty>,
              "SetDuty must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<SetBrightness>,
              "SetBrightness must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<Pulse>,
              "Pulse must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<Glitter>,
              "Glitter must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<Animation>,
              "Animation must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<StartAnimation>,
              "StartAnimation must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<ClearAnimations>,
              "ClearAnimations must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<LedCommand>,
              "LedCommand must remain queue-copyable");

} // namespace Totem::LedPwm
