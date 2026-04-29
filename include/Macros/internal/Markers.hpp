// IWYU pragma: private

#pragma once

#ifdef __clang__
#define WIRE_MSG [[clang::annotate("wire")]]
#define BINDING [[clang::annotate("binding")]]
#else
#define WIRE_MSG
#define BINDING
#endif
