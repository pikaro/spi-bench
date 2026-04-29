// IWYU pragma: private

#pragma once

#ifndef RESTRICT
#if defined(__clang__) || defined(__GNUC__)
#define RESTRICT __restrict__
#elif defined(_MSC_VER)
#define RESTRICT __restrict
#else
#define RESTRICT
#endif
#endif
