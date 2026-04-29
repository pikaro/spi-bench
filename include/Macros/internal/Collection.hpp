// IWYU pragma: private

#pragma once

#define FIND_IN_VEC(vec, name)                                                 \
    std::ranges::find((vec).begin(), (vec).end(), name)
#define IN_VEC(vec, name) FIND_IN_VEC(vec, name) != (vec).end()
#define NOT_IN_VEC(vec, name) FIND_IN_VEC(vec, name) == (vec).end()
#define NO_MATCH(vec) vec.end()

#define INTERNAL_TRANSITION_DEFAULT(name, fromState, toState)                  \
    Totem::Generic::detail::StateTransition<CONCAT(name, State),               \
                                            CONCAT(name, Event)> {             \
        .from = CONCAT(name, State)::fromState,                                \
        .to = CONCAT(name, State)::toState,                                    \
        .event = CONCAT(name, Event)::Default                                  \
    }
#define INTERNAL_TRANSITION_EVENT(name, fromState, toState, eventName)         \
    Totem::Generic::detail::StateTransition<CONCAT(name, State),               \
                                            CONCAT(name, Event)> {             \
        .from = CONCAT(name, State)::fromState,                                \
        .to = CONCAT(name, State)::toState,                                    \
        .event = CONCAT(name, Event)::eventName                                \
    }
#define TRANSITION(...)                                                        \
    INTERNAL_GET_MACRO_4(__VA_ARGS__, INTERNAL_TRANSITION_EVENT,               \
                         INTERNAL_TRANSITION_DEFAULT)(__VA_ARGS__)
