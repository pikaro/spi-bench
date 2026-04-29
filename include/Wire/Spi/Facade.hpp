#pragma once

#include "Wire/Spi/detail/Master.hpp"
#include "Wire/Spi/detail/Slave.hpp"
#include "Wire/Spi/detail/Types.hpp"

namespace Totem::Wire::Spi {

using detail::DeviceHandle;
using detail::Master;
using detail::Slave;
using detail::Transfer;
using detail::TransferResult;

} // namespace Totem::Wire::Spi
