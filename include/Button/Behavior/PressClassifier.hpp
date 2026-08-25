#pragma once

#include "Button/Interfaces/Types.hpp"
#include "Generic/InlineCallback.hpp"
#include <atomic>
#include <cstdint>
#include <optional>
#include <utility>

namespace Totem::Button::Behavior {

struct PressConfig {
    std::optional<uint16_t> longPressMs = std::nullopt;
    std::optional<uint16_t> doublePressMs = std::nullopt;
};

/**
 * Classifies pressed/released transitions into complete button gestures.
 *
 * onButton() may run from ISR context while work() handles elapsed time from a
 * task. Both arbitrate through one packed 32-bit atomic state. Output callbacks
 * therefore run in whichever context completes the gesture and must be safe in
 * both contexts.
 */
class PressClassifier {
  public:
    template <typename Callback>
    explicit PressClassifier(Callback callback, PressConfig config = {})
        : _callback(std::move(callback)), _config(config) {}

    PressClassifier(const PressClassifier &) = delete;
    PressClassifier &operator=(const PressClassifier &) = delete;
    PressClassifier(PressClassifier &&) = delete;
    PressClassifier &operator=(PressClassifier &&) = delete;

    /** Supply the observation time alongside the hardware button transition. */
    void onButton(Event event, uint32_t nowMs) {
        switch (event) {
        case Event::Pressed:
            _pressed(nowMs);
            return;
        case Event::Released:
            _released(nowMs);
            return;
        case Event::Press:
        case Event::LongPress:
        case Event::DoublePress:
            return;
        }
    }

    /** Fire any gesture whose configured timeout has elapsed. */
    void work(uint32_t nowMs) {
        uint32_t current = _state.load(std::memory_order_acquire);
        for (;;) {
            switch (_phase(current)) {
            case Phase::FirstDown:
                if (!_longPressElapsed(current, nowMs)) {
                    return;
                }
                if (_state.compare_exchange_weak(
                        current, _pack(Phase::LongHeld, nowMs),
                        std::memory_order_acq_rel, std::memory_order_acquire)) {
                    _callback(Event::LongPress);
                    return;
                }
                break;

            case Phase::AwaitSecond:
                if (!_doublePressElapsed(current, nowMs)) {
                    return;
                }
                if (_state.compare_exchange_weak(
                        current, _pack(Phase::Idle, nowMs),
                        std::memory_order_acq_rel, std::memory_order_acquire)) {
                    _callback(Event::Press);
                    return;
                }
                break;

            case Phase::SecondDown:
                if (!_longPressElapsed(current, nowMs)) {
                    return;
                }
                if (_state.compare_exchange_weak(
                        current, _pack(Phase::LongHeld, nowMs),
                        std::memory_order_acq_rel, std::memory_order_acquire)) {
                    // Preserve the first completed press when the second press
                    // turns into a long press instead of a double press.
                    _callback(Event::Press);
                    _callback(Event::LongPress);
                    return;
                }
                break;

            case Phase::Idle:
            case Phase::LongHeld:
                return;
            }
        }
    }

    /** Cancel a partial gesture without producing an event. */
    void reset() {
        _state.store(_pack(Phase::Idle, 0), std::memory_order_release);
    }

  private:
    enum class Phase : uint8_t {
        Idle,
        FirstDown,
        AwaitSecond,
        SecondDown,
        LongHeld,
    };

    void _pressed(uint32_t nowMs) {
        uint32_t current = _state.load(std::memory_order_acquire);
        for (;;) {
            switch (_phase(current)) {
            case Phase::Idle:
                if (_state.compare_exchange_weak(
                        current, _pack(Phase::FirstDown, nowMs),
                        std::memory_order_acq_rel, std::memory_order_acquire)) {
                    return;
                }
                break;

            case Phase::AwaitSecond: {
                const bool expired = _doublePressElapsed(current, nowMs);
                const auto next =
                    expired ? Phase::FirstDown : Phase::SecondDown;
                if (_state.compare_exchange_weak(current, _pack(next, nowMs),
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire)) {
                    if (expired) {
                        _callback(Event::Press);
                    }
                    return;
                }
                break;
            }

            case Phase::FirstDown:
            case Phase::SecondDown:
            case Phase::LongHeld:
                return;
            }
        }
    }

    void _released(uint32_t nowMs) {
        uint32_t current = _state.load(std::memory_order_acquire);
        for (;;) {
            switch (_phase(current)) {
            case Phase::FirstDown: {
                const bool longPress = _longPressElapsed(current, nowMs);
                const auto next =
                    !longPress && _config.doublePressMs.has_value()
                        ? Phase::AwaitSecond
                        : Phase::Idle;
                if (_state.compare_exchange_weak(current, _pack(next, nowMs),
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire)) {
                    if (longPress) {
                        _callback(Event::LongPress);
                    } else if (next == Phase::Idle) {
                        _callback(Event::Press);
                    }
                    return;
                }
                break;
            }

            case Phase::SecondDown: {
                const bool longPress = _longPressElapsed(current, nowMs);
                if (_state.compare_exchange_weak(
                        current, _pack(Phase::Idle, nowMs),
                        std::memory_order_acq_rel, std::memory_order_acquire)) {
                    if (longPress) {
                        _callback(Event::Press);
                        _callback(Event::LongPress);
                    } else {
                        _callback(Event::DoublePress);
                    }
                    return;
                }
                break;
            }

            case Phase::LongHeld:
                if (_state.compare_exchange_weak(
                        current, _pack(Phase::Idle, nowMs),
                        std::memory_order_acq_rel, std::memory_order_acquire)) {
                    return;
                }
                break;

            case Phase::Idle:
            case Phase::AwaitSecond:
                return;
            }
        }
    }

    [[nodiscard]] bool _longPressElapsed(uint32_t state, uint32_t nowMs) const {
        return _config.longPressMs.has_value() &&
               _elapsed(state, nowMs) >= *_config.longPressMs;
    }

    [[nodiscard]] bool _doublePressElapsed(uint32_t state,
                                           uint32_t nowMs) const {
        return _config.doublePressMs.has_value() &&
               _elapsed(state, nowMs) >= *_config.doublePressMs;
    }

    [[nodiscard]] static constexpr uint32_t _pack(Phase phase, uint32_t nowMs) {
        return (static_cast<uint32_t>(phase) << phaseShift) |
               (nowMs & timestampMask);
    }

    [[nodiscard]] static constexpr Phase _phase(uint32_t state) {
        return static_cast<Phase>(state >> phaseShift);
    }

    [[nodiscard]] static constexpr uint32_t _elapsed(uint32_t state,
                                                     uint32_t nowMs) {
        return ((nowMs & timestampMask) - (state & timestampMask)) &
               timestampMask;
    }

    Generic::InlineCallback<Event> _callback;
    PressConfig _config;
    std::atomic<uint32_t> _state{_pack(Phase::Idle, 0)};

    // Five phases leave 29 timestamp bits. uint16_t timeouts are comfortably
    // below half this modulo range, so ordinary wrap-safe subtraction applies.
    static constexpr uint32_t phaseShift = 29U;
    static constexpr uint32_t timestampMask = (1U << phaseShift) - 1U;
};

} // namespace Totem::Button::Behavior
