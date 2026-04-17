#pragma once

#define FIND_IN_VEC(vec, name)                                                 \
    std::ranges::find((vec).begin(), (vec).end(), name)
#define IN_VEC(vec, name) FIND_IN_VEC(vec, name) != (vec).end()
#define NOT_IN_VEC(vec, name) FIND_IN_VEC(vec, name) == (vec).end()
#define NO_MATCH(vec) vec.end()
