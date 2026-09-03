#pragma once

#include "Types/Error.hpp"
#include <cstdint>
#include <expected>

namespace Totem::StatusLed {

enum class StateKind : uint8_t {
    Informational = 0,
    Warning,
    Error,
    Critical,
};

struct RgbColor {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;

    [[nodiscard]] constexpr bool operator==(const RgbColor &) const = default;
    [[nodiscard]] constexpr bool any() const {
        return red != 0 || green != 0 || blue != 0;
    }
};

/** Linear per-channel brightness multiplier expressed as a whole percent. */
struct BrightnessMultiplier {
    static constexpr uint8_t fullPercent = 100;

    uint8_t percent = fullPercent;

    [[nodiscard]] static constexpr BrightnessMultiplier
    fromPercent(uint8_t percent) {
        return BrightnessMultiplier{.percent = percent};
    }

    [[nodiscard]] constexpr bool validate() const {
        return percent <= fullPercent;
    }

    /**
     * Scales each RGB channel and truncates fractional channel values.
     * Requires `validate()` to be true.
     */
    [[nodiscard]] constexpr RgbColor apply(RgbColor color) const {
        return RgbColor{
            .red = scale(color.red),
            .green = scale(color.green),
            .blue = scale(color.blue),
        };
    }

  private:
    [[nodiscard]] constexpr uint8_t scale(uint8_t value) const {
        return static_cast<uint8_t>((static_cast<uint16_t>(value) * percent) /
                                    fullPercent);
    }
};

struct StateDef {
    const char *name = nullptr;
    RgbColor color{};
    StateKind kind = StateKind::Informational;

    [[nodiscard]] constexpr bool validate() const {
        return name != nullptr && name[0] != '\0';
    }
};

struct StateHandle {
    void *ctx = nullptr;
    uint8_t id = 0xFF;
    ReturnCode (*setState)(void *ctx, uint8_t id, bool active) = nullptr;

    [[nodiscard]] constexpr bool valid() const {
        return ctx != nullptr && setState != nullptr && id != 0xFF;
    }

    ReturnCode set(bool active = true) const {
        if (!valid()) {
            return ReturnCode::from(CoreError::InvalidState);
        }
        return setState(ctx, id, active);
    }

    ReturnCode reset() const { return set(false); }
};

struct Directory {
    void *ctx = nullptr;
    std::expected<StateHandle, ReturnCode> (*registerStateFn)(void *ctx,
                                                              StateDef def) =
        nullptr;

    [[nodiscard]] constexpr bool valid() const {
        return ctx != nullptr && registerStateFn != nullptr;
    }

    std::expected<StateHandle, ReturnCode> registerState(StateDef def) const {
        if (!valid()) {
            return std::unexpected(ReturnCode::from(CoreError::InvalidState));
        }
        return registerStateFn(ctx, def);
    }

    ReturnCode set(StateHandle handle, bool active = true) const {
        if (!valid()) {
            return ReturnCode::from(CoreError::InvalidState);
        }
        if (handle.ctx != ctx) {
            return ReturnCode::from(CoreError::InvalidArgument);
        }
        return handle.set(active);
    }

    ReturnCode reset(StateHandle handle) const { return set(handle, false); }
};

} // namespace Totem::StatusLed
