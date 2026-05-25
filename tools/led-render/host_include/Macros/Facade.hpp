#pragma once

#include "Macros/internal/Markers.hpp"

#define DELETE_COPY(Type)                                                      \
    Type(const Type &) = delete;                                               \
    Type &operator=(const Type &) = delete;

#define DELETE_MOVE(Type)                                                      \
    Type(Type &&) = delete;                                                    \
    Type &operator=(Type &&) = delete;
