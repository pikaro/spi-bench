#pragma once

#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <type_traits>

namespace Totem::Generic::detail {

template <typename State> struct StateTransition {
    static_assert(std::is_enum_v<State>, "State must be an enum");

    State from;
    State to;
};

template <typename State> constexpr std::size_t stateIndex(State state) {
    return static_cast<std::size_t>(state);
}

template <typename State> constexpr std::size_t stateCount() {
    return magic_enum::enum_count<State>();
}

[[noreturn]] void abortInvalidState();

template <typename State, auto Transitions> class StateMachine {
    static_assert(std::is_enum_v<State>, "State must be an enum");
    static_assert(State::Invalid == static_cast<State>(0),
                  "State enum must have an Invalid value at 0");

  private:
    static constexpr std::size_t StateCount = stateCount<State>();

    using LookupTable = std::array<std::optional<State>, StateCount>;

    static constexpr LookupTable makeNextLookup() {
        LookupTable lookup{};
        lookup.fill(State::Invalid);

        for (const auto &transition : Transitions) {
            lookup[stateIndex(transition.from)] = transition.to;
        }

        return lookup;
    }

    static constexpr LookupTable makePreviousLookup() {
        LookupTable lookup{};
        lookup.fill(State::Invalid);

        for (const auto &transition : Transitions) {
            lookup[stateIndex(transition.to)] = transition.from;
        }

        return lookup;
    }

    static constexpr LookupTable NextLookup = makeNextLookup();
    static constexpr LookupTable PreviousLookup = makePreviousLookup();

  public:
    constexpr explicit StateMachine(
        const char *ownerName, State startState,
        std::optional<State> endState = std::nullopt)
        : _ownerName(ownerName), _state(startState), _startState(startState),
          _endState(endState) {}

    [[nodiscard]] State current() const { return _state.load(); }
    [[nodiscard]] State start() const { return _startState; }
    [[nodiscard]] std::optional<State> end() const { return _endState; }
    [[nodiscard]] bool is(State state) const { return current() == state; }
    [[nodiscard]] bool isNext(State state) const {
        auto next = nextStateFor(current());
        return next == state;
    }
    [[nodiscard]] bool isStart() const { return is(_startState); }
    [[nodiscard]] bool isEnd() const { return _endState && is(*_endState); }
    [[nodiscard]] State nextState() const { return nextStateFor(current()); }

    [[nodiscard]] static constexpr State nextStateFor(State current) {
        const auto next = NextLookup[stateIndex(current)];

        if (!next) {
            if consteval {
                abortInvalidState(); // compile-time error if evaluated here
            } else {
                return State::Invalid;
            }
        }

        return *next;
    }

    [[nodiscard]] static constexpr State previousStateFor(State next) {
        const auto previous = PreviousLookup[stateIndex(next)];

        if (!previous) {
            if consteval {
                abortInvalidState(); // compile-time error if evaluated here
            } else {
                return State::Invalid;
            }
        }

        return *previous;
    }

    ReturnCode transitionTo(State next) {
        auto expectedCurrent = previousStateFor(next);
        FAIL_IF(expectedCurrent == State::Invalid, ERR(CoreError, InvalidState),
                "Failed to get expected current state for " SV_FMT " in %s",
                MAGIC_SV_ARG(next), _ownerName);

        FAIL_IF_NOT_STATE(_state, expectedCurrent, next, "%s", _ownerName);

        return OK();
    }

    ReturnCode step() {
        FAIL_IF(_endState && current() == *_endState,
                ERR(CoreError, InvalidState), "already in end state " SV_FMT,
                MAGIC_SV_ARG(*_endState));

        auto next = nextStateFor(current());
        FAIL_IF(next == State::Invalid, ERR(CoreError, InvalidState),
                "Failed to get next state for " SV_FMT " in %s",
                MAGIC_SV_ARG(current()), _ownerName);

        FAIL_IF_NOT_STATE(_state, previousStateFor(next), next, "%s",
                          _ownerName);

        return OK();
    }

    void reset() { reset(_startState); }

    void reset(State state) { _state.store(state, std::memory_order_release); }

  private:
    const char *_ownerName;
    std::atomic<State> _state;
    State _startState;
    std::optional<State> _endState;
};

} // namespace Totem::Generic::detail

using Totem::Generic::detail::StateMachine;
