#pragma once

#include "Macros/Facade.hpp"
#include "Types/Error.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <type_traits>

namespace Totem::Generic::detail {

[[noreturn]] void abortInvalidState();

template <typename State, typename Event = void> struct StateTransition;

template <typename State> struct StateTransition<State, void> {
    static_assert(std::is_enum_v<State>, "State must be an enum");

    State from;
    State to;
};

template <typename State, typename Event> struct StateTransition {
    static_assert(std::is_enum_v<State>, "State must be an enum");
    static_assert(std::is_enum_v<Event>, "Event must be an enum");

    State from;
    State to;

    // Event value 0 means "eventless/default transition".
    Event event = Event::Default;
};

template <typename Enum> constexpr std::size_t enumCount() {
    return magic_enum::enum_count<Enum>();
}

template <typename Enum>
constexpr std::optional<std::size_t> enumIndex(Enum value) {
    return magic_enum::enum_index(value);
}

template <typename Enum> constexpr bool isValidEnum(Enum value) {
    return magic_enum::enum_contains(value);
}

template <typename State, typename Event, auto Transitions> class StateMachine {
    static_assert(std::is_enum_v<State>, "State must be an enum");
    static_assert(State::Invalid == static_cast<State>(0),
                  "State enum must have Invalid at value 0");

    static_assert(std::is_enum_v<Event>, "Event must be an enum");
    static_assert(Event::Default == static_cast<Event>(0),
                  "Event enum must have Default at value 0");

  private:
    static constexpr std::size_t StateCount = enumCount<State>();
    static constexpr std::size_t EventCount = enumCount<Event>();
    static constexpr std::size_t TransitionCount = std::size(Transitions);

    using StateTable = std::array<std::optional<State>, StateCount>;
    using CountTable = std::array<std::uint8_t, StateCount>;
    using EventCountTable =
        std::array<std::array<std::uint8_t, EventCount>, StateCount>;
    using EventLookupTable =
        std::array<std::array<std::optional<State>, EventCount>, StateCount>;
    using AdjacencyTable = std::array<std::array<bool, StateCount>, StateCount>;

    static constexpr std::size_t checkedStateIndex(State state) {
        const auto index = enumIndex(state);

        if (!index) {
            if consteval {
                abortInvalidState();
            } else {
                return StateCount;
            }
        }

        return *index;
    }

    static constexpr std::size_t checkedEventIndex(Event event) {
        const auto index = enumIndex(event);

        if (!index) {
            if consteval {
                abortInvalidState();
            } else {
                return EventCount;
            }
        }

        return *index;
    }

    static constexpr bool transitionStatesAreValid() {
        for (const auto &transition : Transitions) {
            if (!isValidEnum(transition.from) || !isValidEnum(transition.to)) {
                return false;
            }

            if (transition.from == State::Invalid ||
                transition.to == State::Invalid) {
                return false;
            }
        }

        return true;
    }

    static constexpr bool transitionEventsAreValid() {
        for (const auto &transition : Transitions) {
            if (!isValidEnum(transition.event)) {
                return false;
            }
        }

        return true;
    }

    static constexpr bool hasDuplicateEventTransitions() {
        for (std::size_t i = 0; i < TransitionCount; ++i) {
            for (std::size_t j = i + 1; j < TransitionCount; ++j) {
                if (Transitions[i].from == Transitions[j].from &&
                    Transitions[i].event == Transitions[j].event) {
                    return true;
                }
            }
        }

        return false;
    }

    static constexpr bool hasDuplicateEdges() {
        for (std::size_t i = 0; i < TransitionCount; ++i) {
            for (std::size_t j = i + 1; j < TransitionCount; ++j) {
                if (Transitions[i].from == Transitions[j].from &&
                    Transitions[i].to == Transitions[j].to &&
                    Transitions[i].event == Transitions[j].event) {
                    return true;
                }
            }
        }

        return false;
    }

    static constexpr bool transitionsAreValid() {
        return transitionStatesAreValid() && transitionEventsAreValid() &&
               !hasDuplicateEdges() && !hasDuplicateEventTransitions();
    }

    static constexpr AdjacencyTable makeAdjacencyLookup() {
        AdjacencyTable lookup{};

        for (const auto &transition : Transitions) {
            lookup[checkedStateIndex(transition.from)]
                  [checkedStateIndex(transition.to)] = true;
        }

        return lookup;
    }

    static constexpr EventLookupTable makeEventLookup() {
        EventLookupTable lookup{};

        for (const auto &transition : Transitions) {
            lookup[checkedStateIndex(transition.from)]
                  [checkedEventIndex(transition.event)] = transition.to;
        }

        return lookup;
    }

    static constexpr CountTable makeOutgoingCounts() {
        CountTable counts{};

        for (const auto &transition : Transitions) {
            ++counts[checkedStateIndex(transition.from)];
        }

        return counts;
    }

    static constexpr CountTable makeIncomingCounts() {
        CountTable counts{};

        for (const auto &transition : Transitions) {
            ++counts[checkedStateIndex(transition.to)];
        }

        return counts;
    }

    static constexpr EventCountTable makeOutgoingEventCounts() {
        EventCountTable counts{};

        for (const auto &transition : Transitions) {
            ++counts[checkedStateIndex(transition.from)]
                    [checkedEventIndex(transition.event)];
        }

        return counts;
    }

    static constexpr StateTable makeUniqueNextLookup() {
        StateTable lookup{};

        for (const auto &transition : Transitions) {
            const auto from = checkedStateIndex(transition.from);

            if (!lookup[from]) {
                lookup[from] = transition.to;
            } else if (*lookup[from] != transition.to) {
                lookup[from] = State::Invalid;
            }
        }

        return lookup;
    }

    static constexpr StateTable makeUniquePreviousLookup() {
        StateTable lookup{};

        for (const auto &transition : Transitions) {
            const auto toState = checkedStateIndex(transition.to);

            if (!lookup[toState]) {
                lookup[toState] = transition.from;
            } else if (*lookup[toState] != transition.from) {
                lookup[toState] = State::Invalid;
            }
        }

        return lookup;
    }

    static constexpr AdjacencyTable AdjacencyLookup = makeAdjacencyLookup();
    static constexpr EventLookupTable EventLookup = makeEventLookup();
    static constexpr CountTable OutgoingCounts = makeOutgoingCounts();
    static constexpr CountTable IncomingCounts = makeIncomingCounts();
    static constexpr EventCountTable OutgoingEventCounts =
        makeOutgoingEventCounts();
    static constexpr StateTable UniqueNextLookup = makeUniqueNextLookup();
    static constexpr StateTable UniquePreviousLookup =
        makeUniquePreviousLookup();

    static_assert(transitionsAreValid(),
                  "StateMachine transitions must use valid states/events, "
                  "must not use State::Invalid, and must not contain duplicate "
                  "(from, event) transitions");

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
    [[nodiscard]] bool isStart() const { return is(_startState); }
    [[nodiscard]] bool isEnd() const { return _endState && is(*_endState); }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    [[nodiscard]] static constexpr bool canTransition(State fromState,
                                                      State toState) {
        const auto fromIndex = enumIndex(fromState);
        const auto toIndex = enumIndex(toState);

        if (!fromIndex || !toIndex) {
            return false;
        }

        return AdjacencyLookup[*fromIndex][*toIndex];
    }

    [[nodiscard]] static constexpr bool canTransition(State fromState,
                                                      Event event) {
        const auto fromIndex = enumIndex(fromState);
        const auto eventIndex = enumIndex(event);

        if (!fromIndex || !eventIndex) {
            return false;
        }

        return EventLookup[*fromIndex][*eventIndex].has_value();
    }

    [[nodiscard]] static constexpr bool
    canTransition(State fromState, Event event, State toState) {
        const auto next = nextStateFor(fromState, event);
        return next == toState;
    }

    [[nodiscard]] bool canTransitionTo(State next) const {
        return canTransition(current(), next);
    }

    [[nodiscard]] bool canTransitionOn(Event event) const {
        return canTransition(current(), event);
    }

    [[nodiscard]] bool canTransitionOn(Event event, State next) const {
        return canTransition(current(), event, next);
    }

    [[nodiscard]] static constexpr std::size_t outgoingCount(State state) {
        const auto index = enumIndex(state);
        return index ? OutgoingCounts[*index] : 0;
    }

    [[nodiscard]] static constexpr std::size_t incomingCount(State state) {
        const auto index = enumIndex(state);
        return index ? IncomingCounts[*index] : 0;
    }

    [[nodiscard]] static constexpr bool hasUniqueNextState(State state) {
        const auto index = enumIndex(state);
        return index && OutgoingCounts[*index] == 1;
    }

    [[nodiscard]] static constexpr bool hasUniquePreviousState(State state) {
        const auto index = enumIndex(state);
        return index && IncomingCounts[*index] == 1;
    }

    [[nodiscard]] bool isNext(State state) const {
        return canTransition(current(), state);
    }

    [[nodiscard]] State nextState() const { return nextStateFor(current()); }

    [[nodiscard]] static constexpr State nextStateFor(State current) {
        const auto index = enumIndex(current);

        if (!index) {
            if consteval {
                abortInvalidState();
            } else {
                return State::Invalid;
            }
        }

        const auto next = UniqueNextLookup[*index];

        if (!next || *next == State::Invalid) {
            if consteval {
                abortInvalidState();
            } else {
                return State::Invalid;
            }
        }

        return *next;
    }

    [[nodiscard]] static constexpr State nextStateFor(State current,
                                                      Event event) {
        const auto stateIndex = enumIndex(current);
        const auto eventIndex = enumIndex(event);

        if (!stateIndex || !eventIndex) {
            if consteval {
                abortInvalidState();
            } else {
                return State::Invalid;
            }
        }

        const auto next = EventLookup[*stateIndex][*eventIndex];

        if (!next) {
            if consteval {
                abortInvalidState();
            } else {
                return State::Invalid;
            }
        }

        return *next;
    }

    [[nodiscard]] static constexpr State previousStateFor(State next) {
        const auto index = enumIndex(next);

        if (!index) {
            if consteval {
                abortInvalidState();
            } else {
                return State::Invalid;
            }
        }

        const auto previous = UniquePreviousLookup[*index];

        if (!previous || *previous == State::Invalid) {
            if consteval {
                abortInvalidState();
            } else {
                return State::Invalid;
            }
        }

        return *previous;
    }

    ReturnCode transitionTo(State next) {
        const auto currentState = current();

        FAIL_IF(!canTransition(currentState, next),
                ERR(CoreError, InvalidState),
                "invalid state transition " SV_FMT " -> " SV_FMT " in %s",
                MAGIC_SV_ARG(currentState), MAGIC_SV_ARG(next), _ownerName);

        FAIL_IF_NOT_STATE(_state, currentState, next, "%s", _ownerName);

        return OK();
    }

    ReturnCode transition(Event event) {
        const auto currentState = current();
        const auto next = nextStateFor(currentState, event);

        FAIL_IF(next == State::Invalid, ERR(CoreError, InvalidState),
                "invalid event " SV_FMT " for state " SV_FMT " in %s",
                MAGIC_SV_ARG(event), MAGIC_SV_ARG(currentState), _ownerName);

        FAIL_IF_NOT_STATE(_state, currentState, next, "%s", _ownerName);

        return OK();
    }

    ReturnCode step() { return transition(Event::Default); }

    ReturnCode step(Event event) { return transition(event); }

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
using Totem::Generic::detail::StateTransition;
