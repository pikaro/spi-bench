#pragma once

#include "Network/Interfaces/Config.hpp"   // IWYU pragma: export
#include "Network/Interfaces/Endpoint.hpp" // IWYU pragma: export
#include "Network/detail/Commands.hpp"
#include "Network/detail/TcpSocket.hpp"
#include "Network/detail/UdpSocket.hpp"

namespace Totem::Network {

using detail::DefaultTcpClient;
using detail::DefaultTcpConnection;
using detail::DefaultTcpListener;
using detail::DefaultUdpSocket;

namespace Commands {

using detail::registerCommands;

} // namespace Commands

} // namespace Totem::Network
