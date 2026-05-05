#pragma once

#include "StaticConfig/LedDisplay.hpp"

#if LED_DISPLAY_GENERIC_RENDERER
#include "LedDisplay/Renderers/GenericRenderer.hpp"
#else
#include "LedDisplay/Renderers/FastLedRenderer.hpp"
#endif

#include "LedDisplay/detail/Renderer.hpp"

namespace Totem::LedDisplay::detail {

#if LED_DISPLAY_GENERIC_RENDERER
using SelectedRenderer = Renderers::GenericRenderer;
#else
using SelectedRenderer = Renderers::FastLedRenderer;
#endif

using Render = Renderer<SelectedRenderer>;

} // namespace Totem::LedDisplay::detail
