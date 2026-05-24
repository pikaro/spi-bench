#pragma once

#include "Network/detail/PlatformSelect.hpp"

namespace Totem::Network::detail {

using DefaultTcpClient = TcpClient;
using DefaultTcpConnection = TcpConnection;
using DefaultTcpListener = TcpListener;

} // namespace Totem::Network::detail
