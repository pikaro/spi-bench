#pragma once

#ifdef __clang__
#define WIRE_MSG [[clang::annotate("wire")]]
#else
#define WIRE_MSG
#endif
