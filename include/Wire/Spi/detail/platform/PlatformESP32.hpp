#pragma once

#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "RingBuffer/detail/Config.hpp"
#include "Types/Error.hpp"
#include "freertos/projdefs.h"
#include "freertos/ringbuf.h"
#include <cstddef>
#include <expected>
#include <utility>

namespace Totem::Wire::Spi::detail::platform {

struct Platform {};

} // namespace Totem::Wire::Spi::detail::platform
