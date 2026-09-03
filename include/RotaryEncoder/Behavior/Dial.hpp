#pragma once

#include "Generic/InlineCallback.hpp"
#include "RotaryEncoder/Interfaces/PositionConfig.hpp"
#include "RotaryEncoder/Interfaces/Types.hpp"
#include <atomic>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace Totem::RotaryEncoder::Behavior {

struct DialConfig {
    PositionConfig position{
        .initialValue = 0,
        .minimum = 0,
        .maximum = 255,
    };

    [[nodiscard]] constexpr bool validate() const {
        return position.validate() && position.minimum.has_value() &&
               position.maximum.has_value() &&
               *position.minimum < *position.maximum;
    }

    [[nodiscard]] constexpr std::optional<uint8_t>
    valueFor(int32_t value) const {
        if (!validate() || value < *position.minimum ||
            value > *position.maximum) {
            return std::nullopt;
        }

        const auto minimum = static_cast<int64_t>(*position.minimum);
        const auto maximum = static_cast<int64_t>(*position.maximum);
        const auto range = static_cast<uint64_t>(maximum - minimum);
        const auto offset =
            static_cast<uint64_t>(static_cast<int64_t>(value) - minimum);
        constexpr uint64_t fullScale = 255U;
        return static_cast<uint8_t>(((offset * fullScale) + (range / 2U)) /
                                    range);
    }
};

struct DialEvent {
    int32_t position;
    Direction direction;
    uint8_t value;
};

static_assert(sizeof(DialEvent) == 8);
static_assert(std::is_trivially_copyable_v<DialEvent>);

/** Bounded ordinary-dial position and normalized-value behavior. */
class Dial {
  public:
    template <typename Callback>
    Dial(Callback callback, DialConfig config)
        : _callback(std::move(callback)), _config(config),
          _position(config.position.initialValue) {}

    Dial(const Dial &) = delete;
    Dial &operator=(const Dial &) = delete;
    Dial(Dial &&) = delete;
    Dial &operator=(Dial &&) = delete;

    void onRotation(Direction direction) {
        int32_t current = _position.load(std::memory_order_acquire);
        for (;;) {
            const auto next = _config.position.advance(current, direction);
            if (!next.has_value()) {
                return;
            }
            const auto value = _config.valueFor(*next);
            if (!value.has_value()) {
                return;
            }
            if (_position.compare_exchange_weak(current, *next,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
                _callback(DialEvent{
                    .position = *next,
                    .direction = direction,
                    .value = *value,
                });
                return;
            }
        }
    }

    [[nodiscard]] int32_t position() const {
        return _position.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint8_t value() const {
        return _config.valueFor(position()).value_or(0);
    }

  private:
    Generic::InlineCallback<DialEvent> _callback;
    DialConfig _config;
    std::atomic<int32_t> _position;
};

} // namespace Totem::RotaryEncoder::Behavior
