#pragma once

#include "StaticConfig/LedDisplay.hpp"

#if LED_OUTPUT_SK9822_SPI
#include "LedDisplay/Outputs/Sk9822SpiOutput.hpp"
#else
#include "LedDisplay/Outputs/FastLedOutput.hpp"
#endif

namespace Totem::LedDisplay::Outputs {

#if LED_OUTPUT_SK9822_SPI
using SelectedOutput = Sk9822SpiOutput;
#else
using SelectedOutput = FastLedOutput;
#endif

} // namespace Totem::LedDisplay::Outputs
