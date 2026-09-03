#pragma once

#include "Button/Interfaces/Types.hpp"
#include "Generic/InlineCallback.hpp"
#include "RotaryEncoder/Interfaces/PositionConfig.hpp"
#include "RotaryEncoder/Interfaces/Types.hpp"
#include <atomic>
#include <bit>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace Totem::RotaryEncoder::Behavior {

enum class ButtonMenuEventType : uint8_t {
    Shown,
    MovedClockwise,
    MovedCounterclockwise,
    Selected,
};

struct ButtonMenuEvent {
    int32_t position;
    ButtonMenuEventType event;
};

static_assert(sizeof(ButtonMenuEvent) == 8);
static_assert(std::is_trivially_copyable_v<ButtonMenuEvent>);

/**
 * Routes ordinary rotation directly while reserving a held-button gesture for
 * menu selection.
 *
 * Button-down starts a menu gesture at the configured initial position.
 * Rotation while held updates a signed position without invoking the ordinary
 * callback. The menu callback receives absolute show, movement, and selection
 * snapshots. Button-up always selects, including when the position is zero.
 * The packed atomic state keeps button callbacks from task context and encoder
 * callbacks from GPIO ISR context coherent.
 */
class ButtonMenu {
  public:
    template <typename RotationCallback, typename MenuCallback>
    ButtonMenu(RotationCallback rotationCallback, MenuCallback menuCallback,
               PositionConfig positionConfig = {})
        : _rotationCallback(std::move(rotationCallback)),
          _menuCallback(std::move(menuCallback)),
          _positionConfig(positionConfig) {}

    ButtonMenu(const ButtonMenu &) = delete;
    ButtonMenu &operator=(const ButtonMenu &) = delete;
    ButtonMenu(ButtonMenu &&) = delete;
    ButtonMenu &operator=(ButtonMenu &&) = delete;

    void onButton(Button::Event event) {
        if (event == Button::Event::Pressed) {
            const auto initialPosition = _positionConfig.initialValue;
            _state.store(openingMask | _encodedPosition(initialPosition),
                         std::memory_order_release);
            _menuCallback(ButtonMenuEvent{
                .position = initialPosition,
                .event = ButtonMenuEventType::Shown,
            });
            _state.store(activeMask | _encodedPosition(initialPosition),
                         std::memory_order_release);
            return;
        }
        if (event != Button::Event::Released) {
            return;
        }

        const uint32_t completed =
            _state.exchange(0, std::memory_order_acq_rel);
        if ((completed & activeMask) != 0) {
            _menuCallback(ButtonMenuEvent{
                .position = _position(completed),
                .event = ButtonMenuEventType::Selected,
            });
        }
    }

    void onRotation(Direction direction) {
        uint32_t current = _state.load(std::memory_order_acquire);
        if ((current & openingMask) != 0) {
            return;
        }
        while ((current & activeMask) != 0) {
            const int32_t position = _position(current);
            const auto nextPosition = _advance(position, direction);
            if (!nextPosition.has_value()) {
                return;
            }
            const uint32_t next = activeMask | _encodedPosition(*nextPosition);
            if (_state.compare_exchange_weak(current, next,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
                _menuCallback(ButtonMenuEvent{
                    .position = *nextPosition,
                    .event = direction == Direction::Clockwise
                                 ? ButtonMenuEventType::MovedClockwise
                                 : ButtonMenuEventType::MovedCounterclockwise,
                });
                return;
            }
            if ((current & openingMask) != 0) {
                return;
            }
        }

        _rotationCallback(direction);
    }

  private:
    [[nodiscard]] static constexpr uint32_t _encodedPosition(int32_t position) {
        return static_cast<uint32_t>(position) & positionMask;
    }

    [[nodiscard]] static constexpr int32_t _position(uint32_t state) {
        uint32_t encoded = state & positionMask;
        if ((encoded & positionSignMask) != 0) {
            encoded |= ~positionMask;
        }
        return std::bit_cast<int32_t>(encoded);
    }

    [[nodiscard]] std::optional<int32_t> _advance(int32_t position,
                                                  Direction direction) const {
        if ((direction == Direction::Clockwise && position == maxPosition) ||
            (direction == Direction::Counterclockwise &&
             position == minPosition)) {
            return std::nullopt;
        }
        return _positionConfig.advance(position, direction);
    }

    Generic::InlineCallback<Direction> _rotationCallback;
    Generic::InlineCallback<ButtonMenuEvent> _menuCallback;
    PositionConfig _positionConfig;
    std::atomic<uint32_t> _state{0};

    static constexpr uint32_t activeMask = 1U << 31U;
    static constexpr uint32_t openingMask = 1U << 30U;
    static constexpr uint32_t positionMask = openingMask - 1U;
    static constexpr uint32_t positionSignMask = 1U << 29U;
    static constexpr int32_t maxPosition =
        static_cast<int32_t>(positionSignMask - 1U);
    static constexpr int32_t minPosition = -maxPosition - 1;
};

} // namespace Totem::RotaryEncoder::Behavior
