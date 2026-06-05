#pragma once

#include "LedDisplay/Interfaces/AnimationCommand.hpp" // IWYU pragma: export
#include "LedDisplay/Interfaces/AnimationKind.hpp"    // IWYU pragma: export
#include "LedDisplay/Interfaces/AnimationStyle.hpp"   // IWYU pragma: export
#include "LedDisplay/Interfaces/Blend.hpp"            // IWYU pragma: export
#include "LedDisplay/Interfaces/Color.hpp"            // IWYU pragma: export
#include "LedDisplay/Interfaces/Config.hpp"           // IWYU pragma: export
#include "LedDisplay/Interfaces/Layer.hpp"            // IWYU pragma: export
#include "LedDisplay/Interfaces/LayerControl.hpp"     // IWYU pragma: export
#include "LedDisplay/detail/Display.hpp"

namespace Totem::LedDisplay {

using detail::Display;

} // namespace Totem::LedDisplay
