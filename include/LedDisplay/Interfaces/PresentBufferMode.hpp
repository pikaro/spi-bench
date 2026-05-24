#pragma once

#include <cstddef>
#include <cstdint>

namespace Totem::LedDisplay {

enum class PresentBufferMode : uint8_t {
    None = 0,
    Double,
    Triple,
    Quadruple,
};

[[nodiscard]] constexpr size_t
presentBufferCount(PresentBufferMode mode) {
    switch (mode) {
    case PresentBufferMode::None:
        return 1;
    case PresentBufferMode::Double:
        return 2;
    case PresentBufferMode::Triple:
        return 3;
    case PresentBufferMode::Quadruple:
        return 4;
    }
    return 1;
}

} // namespace Totem::LedDisplay
