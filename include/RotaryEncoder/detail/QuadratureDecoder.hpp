// IWYU pragma: private

#pragma once

#include "RotaryEncoder/Interfaces/Types.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace Totem::RotaryEncoder::detail {

struct DecoderResult {
    std::optional<Direction> direction = std::nullopt;
    bool invalidTransition = false;
};

/**
 * Compact Gray-code decoder for a two-channel mechanical encoder.
 *
 * Each legal adjacent transition contributes +1 or -1. A direction change
 * starts a new partial detent so a stale phase offset cannot consume the first
 * complete detent after a reversal. Alternating contact bounce therefore
 * cannot accumulate to a complete detent. Skipped diagonal transitions reset
 * the partial detent so they cannot manufacture a turn in either direction.
 */
class QuadratureDecoder {
  public:
    constexpr void reset(uint8_t state) {
        _previousState = static_cast<uint8_t>(state & stateMask);
        _accumulator = 0;
    }

    [[nodiscard]] constexpr DecoderResult
    update(uint8_t state, uint8_t transitionsPerDetent, bool reverseDirection) {
        state = static_cast<uint8_t>(state & stateMask);
        if (state == _previousState) {
            return {};
        }

        const auto index = static_cast<uint8_t>((_previousState << 2U) | state);
        const int8_t transition = transitionDeltas[index];
        _previousState = state;

        if (transition == 0) {
            _accumulator = 0;
            return {.invalidTransition = true};
        }

        const bool directionChanged = (_accumulator > 0 && transition < 0) ||
                                      (_accumulator < 0 && transition > 0);
        _accumulator = directionChanged
                           ? transition
                           : static_cast<int8_t>(_accumulator + transition);
        const int8_t threshold = static_cast<int8_t>(transitionsPerDetent);
        if (_accumulator < threshold && _accumulator > -threshold) {
            return {};
        }

        const bool positive = _accumulator > 0;
        _accumulator = 0;
        const bool clockwise = positive != reverseDirection;
        return {.direction = clockwise ? Direction::Clockwise
                                       : Direction::Counterclockwise};
    }

  private:
    static constexpr uint8_t stateMask = 0b11U;

    // Index: previous AB in the high bits, current AB in the low bits.
    // Positive cycle: 00 -> 01 -> 11 -> 10 -> 00.
    static constexpr std::array<int8_t, 16> transitionDeltas{
        0,  1,  -1, 0,  // 00 -> 00, 01, 10, 11
        -1, 0,  0,  1,  // 01 -> 00, 01, 10, 11
        1,  0,  0,  -1, // 10 -> 00, 01, 10, 11
        0,  -1, 1,  0,  // 11 -> 00, 01, 10, 11
    };

    uint8_t _previousState = 0;
    int8_t _accumulator = 0;
};

namespace tests {

consteval bool decodesFullDetents() {
    QuadratureDecoder decoder{};
    decoder.reset(0b00);
    if (decoder.update(0b01, 4, false).direction.has_value() ||
        decoder.update(0b11, 4, false).direction.has_value() ||
        decoder.update(0b10, 4, false).direction.has_value()) {
        return false;
    }
    if (decoder.update(0b00, 4, false).direction != Direction::Clockwise) {
        return false;
    }

    if (decoder.update(0b10, 4, false).direction.has_value() ||
        decoder.update(0b11, 4, false).direction.has_value() ||
        decoder.update(0b01, 4, false).direction.has_value()) {
        return false;
    }
    return decoder.update(0b00, 4, false).direction ==
           Direction::Counterclockwise;
}

consteval bool rejectsBounceAndSkippedStates() {
    QuadratureDecoder decoder{};
    decoder.reset(0b00);
    if (decoder.update(0b01, 4, false).direction.has_value() ||
        decoder.update(0b00, 4, false).direction.has_value()) {
        return false;
    }

    (void)decoder.update(0b01, 4, false);
    const auto invalid = decoder.update(0b10, 4, false);
    return invalid.invalidTransition && !invalid.direction.has_value();
}

consteval bool decodesReversalWithPartialPhase() {
    QuadratureDecoder decoder{};
    decoder.reset(0b00);

    // Simulate startup between physical detents. Reaching the first stable
    // position leaves one positive transition of phase history.
    if (decoder.update(0b01, 4, false).direction.has_value()) {
        return false;
    }

    // A complete detent in the opposite direction must not be consumed by
    // that stale partial transition.
    if (decoder.update(0b00, 4, false).direction.has_value() ||
        decoder.update(0b10, 4, false).direction.has_value() ||
        decoder.update(0b11, 4, false).direction.has_value()) {
        return false;
    }
    if (decoder.update(0b01, 4, false).direction !=
        Direction::Counterclockwise) {
        return false;
    }

    // Returning by one complete detent must produce the matching event too.
    if (decoder.update(0b11, 4, false).direction.has_value() ||
        decoder.update(0b10, 4, false).direction.has_value() ||
        decoder.update(0b00, 4, false).direction.has_value()) {
        return false;
    }
    return decoder.update(0b01, 4, false).direction == Direction::Clockwise;
}

static_assert(decodesFullDetents());
static_assert(rejectsBounceAndSkippedStates());
static_assert(decodesReversalWithPartialPhase());

} // namespace tests

} // namespace Totem::RotaryEncoder::detail
