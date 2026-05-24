// IWYU pragma: private

#pragma once

#if defined(PLATFORM_ESP32)
#include "Network/detail/platform/PlatformESP32.hpp"
#else
#error "No supported network platform selected"
#endif

namespace Totem::Network::detail {

using platform::EndpointText;
using platform::ReceiveResult;
using platform::TcpClient;
using platform::TcpConnection;
using platform::TcpListener;
using platform::UdpSocket;
using platform::formatEndpoint;
using platform::parseIpv4Endpoint;

} // namespace Totem::Network::detail
