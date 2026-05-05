#pragma once

#include "Wire/I2C/detail/Device.hpp"
#include "Wire/I2C/detail/Master.hpp"
#include "Wire/I2C/detail/Mcp4661.hpp"
#include "Wire/I2C/detail/Pcf8574.hpp"
#include "Wire/I2C/detail/Ssd1306Display.hpp"

namespace Totem::Wire::I2C {

using detail::Device;
using detail::Master;
using detail::Mcp4661;
using detail::Pcf8574;
using detail::Ssd1306Display;

} // namespace Totem::Wire::I2C
