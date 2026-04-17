#pragma once

#include "CommandBackend/Transports/UartTransport.hpp"
#include "CommandBackend/detail/Controller.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "CommandBackend/detail/Registrar.hpp"

namespace Totem::CommandBackend {

using detail::arg;
using detail::Controller;
using detail::Registrar;

using detail::Transports::UartTransport;

} // namespace Totem::CommandBackend
