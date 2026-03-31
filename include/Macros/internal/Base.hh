#pragma once

#define INTERNAL_GET_MACRO_1(_1, NAME, ...) NAME
#define INTERNAL_GET_MACRO_2(_1, _2, NAME, ...) NAME
#define INTERNAL_GET_MACRO_3(_1, _2, _3, NAME, ...) NAME
#define INTERNAL_GET_MACRO_4(_1, _2, _3, _4, NAME, ...) NAME

#define CONCAT(a, b) a##b
