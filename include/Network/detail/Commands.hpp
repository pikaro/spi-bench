#pragma once

#include "CommandBackend/Facade.hpp"
#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Network/detail/TcpSocket.hpp"
#include "Network/detail/UdpSocket.hpp"
#include "Services/Commands.hpp"
#include "StaticConfig/Network.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Totem::Network::detail {

static constexpr LogComponent logComponent = LogComponent::System;

inline std::span<const std::byte> asBytes(std::string_view text) {
    return std::as_bytes(std::span{text.data(), text.size()});
}

inline std::expected<uint16_t, ReturnCode> parsePort(uint32_t value) {
    FAIL_IF(value == 0 || value > 65535U,
            std::unexpected(ERR(CoreError, InvalidArgument)),
            "Invalid network port %lu", static_cast<unsigned long>(value));
    return static_cast<uint16_t>(value);
}

inline std::expected<uint32_t, ReturnCode>
optionalTimeout(CommandDesc::ParsedArgs args, std::size_t index) {
    auto clampTimeout = [](uint32_t timeoutMs) {
        if (timeoutMs > Totem::StaticConfig::Network::maxCommandSocketTimeoutMs) {
            _log_w("Network command timeout capped from %lu ms to %lu ms",
                   static_cast<unsigned long>(timeoutMs),
                   static_cast<unsigned long>(
                       Totem::StaticConfig::Network::maxCommandSocketTimeoutMs));
            return Totem::StaticConfig::Network::maxCommandSocketTimeoutMs;
        }
        return timeoutMs;
    };

    auto parsed = args.get<uint32_t>(index);
    if (parsed.ok) {
        return clampTimeout(parsed.value);
    }
    if (parsed.error == CommandDesc::ArgError::Missing) {
        return clampTimeout(Totem::StaticConfig::Network::defaultSocketTimeoutMs);
    }
    return std::unexpected(ERR(CommandError, BadArgument));
}

inline ReturnCode handleUdpSend(CommandDesc::ParsedArgs args,
                                void * /*ctx*/) {
    const auto host = args.get<std::string_view>(0);
    const auto portArg = args.get<uint32_t>(1);
    const auto payload = args.get<std::string_view>(2);
    FAIL_IF(!host.ok || !portArg.ok || !payload.ok,
            ERR(CommandError, BadArgument), "Invalid /udp-send arguments");
    FAIL_IF(payload.value.empty(), ERR(CommandError, BadArgument),
            "UDP payload cannot be empty");

    FAIL_IF_UNEXPECTED_FWD(port, parsePort(portArg.value),
                           "Invalid UDP port");
    FAIL_IF_UNEXPECTED_FWD(endpoint, parseIpv4Endpoint(host.value, port),
                           "Invalid UDP endpoint");

    DefaultUdpSocket socket{};
    FAIL_IF_ERR_FWD(socket.open(), "Failed to open UDP socket");
    FAIL_IF_ERR_FWD(socket.sendTo(endpoint, asBytes(payload.value)),
                    "Failed to send UDP diagnostic packet");
    const auto text = formatEndpoint(endpoint);
    _log_i("UDP sent %zu bytes to %s", payload.value.size(), text.c_str());
    return OK();
}

inline ReturnCode handleUdpRecv(CommandDesc::ParsedArgs args,
                                void * /*ctx*/) {
    const auto portArg = args.get<uint32_t>(0);
    FAIL_IF(!portArg.ok, ERR(CommandError, BadArgument),
            "Invalid /udp-recv port argument");
    FAIL_IF_UNEXPECTED_FWD(port, parsePort(portArg.value),
                           "Invalid UDP port");
    FAIL_IF_UNEXPECTED_FWD(timeoutMs, optionalTimeout(args, 1),
                           "Invalid UDP receive timeout");

    DefaultUdpSocket socket{};
    FAIL_IF_ERR_FWD(socket.open(port), "Failed to open UDP receive socket");
    _log_i("UDP listening on port %u for %lu ms", static_cast<unsigned>(port),
           static_cast<unsigned long>(timeoutMs));

    std::array<std::byte, Totem::StaticConfig::Network::diagPacketBytes>
        buffer{};
    ReceiveResult received{};
    FAIL_IF_ERR_FWD(socket.receiveFrom(buffer, timeoutMs, received),
                    "Failed to receive UDP diagnostic packet");
    const auto text = formatEndpoint(received.remote);
    _log_i("UDP received %zu bytes from %s: %.*s", received.size,
           text.c_str(), static_cast<int>(received.size),
           reinterpret_cast<const char *>(buffer.data()));
    return OK();
}

inline ReturnCode handleUdpExchange(CommandDesc::ParsedArgs args,
                                    void * /*ctx*/) {
    const auto host = args.get<std::string_view>(0);
    const auto portArg = args.get<uint32_t>(1);
    const auto payload = args.get<std::string_view>(2);
    FAIL_IF(!host.ok || !portArg.ok || !payload.ok,
            ERR(CommandError, BadArgument), "Invalid /udp-exchange arguments");
    FAIL_IF(payload.value.empty(), ERR(CommandError, BadArgument),
            "UDP payload cannot be empty");

    FAIL_IF_UNEXPECTED_FWD(port, parsePort(portArg.value),
                           "Invalid UDP port");
    FAIL_IF_UNEXPECTED_FWD(timeoutMs, optionalTimeout(args, 3),
                           "Invalid UDP exchange timeout");
    uint16_t bindPort = 0;
    const auto bindPortArg = args.get<uint32_t>(4);
    if (bindPortArg.ok) {
        FAIL_IF_UNEXPECTED_FWD(parsedBindPort, parsePort(bindPortArg.value),
                               "Invalid UDP exchange bind port");
        bindPort = parsedBindPort;
    } else if (bindPortArg.error != CommandDesc::ArgError::Missing) {
        return ERR(CommandError, BadArgument);
    }
    FAIL_IF_UNEXPECTED_FWD(endpoint, parseIpv4Endpoint(host.value, port),
                           "Invalid UDP endpoint");

    DefaultUdpSocket socket{};
    FAIL_IF_ERR_FWD(socket.open(bindPort), "Failed to open UDP exchange socket");
    FAIL_IF_ERR_FWD(socket.sendTo(endpoint, asBytes(payload.value)),
                    "Failed to send UDP exchange packet");

    std::array<std::byte, Totem::StaticConfig::Network::diagPacketBytes>
        buffer{};
    ReceiveResult received{};
    FAIL_IF_ERR_FWD(socket.receiveFrom(buffer, timeoutMs, received),
                    "Failed to receive UDP exchange response");
    const auto text = formatEndpoint(received.remote);
    _log_i("UDP exchanged with %s: sent=%zu received=%zu payload=%.*s",
           text.c_str(), payload.value.size(), received.size,
           static_cast<int>(received.size),
           reinterpret_cast<const char *>(buffer.data()));
    return OK();
}

inline ReturnCode handleTcpConnect(CommandDesc::ParsedArgs args,
                                   void * /*ctx*/) {
    const auto host = args.get<std::string_view>(0);
    const auto portArg = args.get<uint32_t>(1);
    const auto payload = args.get<std::string_view>(2);
    FAIL_IF(!host.ok || !portArg.ok || !payload.ok,
            ERR(CommandError, BadArgument), "Invalid /tcp-connect arguments");
    FAIL_IF(payload.value.empty(), ERR(CommandError, BadArgument),
            "TCP payload cannot be empty");

    FAIL_IF_UNEXPECTED_FWD(port, parsePort(portArg.value),
                           "Invalid TCP port");
    FAIL_IF_UNEXPECTED_FWD(timeoutMs, optionalTimeout(args, 3),
                           "Invalid TCP timeout");
    FAIL_IF_UNEXPECTED_FWD(endpoint, parseIpv4Endpoint(host.value, port),
                           "Invalid TCP endpoint");

    DefaultTcpClient client{};
    DefaultTcpConnection connection{};
    FAIL_IF_ERR_FWD(client.connectTo(endpoint, timeoutMs, connection),
                    "Failed to connect TCP diagnostic socket");
    FAIL_IF_ERR_FWD(connection.sendAll(asBytes(payload.value)),
                    "Failed to send TCP diagnostic payload");

    std::array<std::byte, Totem::StaticConfig::Network::diagPacketBytes>
        buffer{};
    std::size_t received = 0;
    FAIL_IF_ERR_FWD(connection.receive(buffer, timeoutMs, received),
                    "Failed to receive TCP diagnostic response");
    const auto text = formatEndpoint(endpoint);
    _log_i("TCP exchanged with %s: sent=%zu received=%zu payload=%.*s",
           text.c_str(), payload.value.size(), received,
           static_cast<int>(received),
           reinterpret_cast<const char *>(buffer.data()));
    return OK();
}

inline ReturnCode handleTcpListen(CommandDesc::ParsedArgs args,
                                  void * /*ctx*/) {
    const auto portArg = args.get<uint32_t>(0);
    FAIL_IF(!portArg.ok, ERR(CommandError, BadArgument),
            "Invalid /tcp-listen port argument");
    FAIL_IF_UNEXPECTED_FWD(port, parsePort(portArg.value),
                           "Invalid TCP listen port");
    FAIL_IF_UNEXPECTED_FWD(timeoutMs, optionalTimeout(args, 1),
                           "Invalid TCP listen timeout");

    DefaultTcpListener listener{};
    FAIL_IF_ERR_FWD(listener.open(port), "Failed to open TCP listener");
    _log_i("TCP listening on port %u for %lu ms",
           static_cast<unsigned>(port),
           static_cast<unsigned long>(timeoutMs));
    DefaultTcpConnection connection{};
    FAIL_IF_ERR_FWD(listener.acceptOne(timeoutMs, connection),
                    "Failed to accept TCP diagnostic client");

    std::array<std::byte, Totem::StaticConfig::Network::diagPacketBytes>
        buffer{};
    std::size_t received = 0;
    FAIL_IF_ERR_FWD(connection.receive(buffer, timeoutMs, received),
                    "Failed to receive TCP diagnostic request");
    FAIL_IF_ERR_FWD(
        connection.sendAll(std::span<const std::byte>(buffer.data(),
                                                      received)),
        "Failed to echo TCP diagnostic response");
    _log_i("TCP received and echoed %zu bytes: %.*s", received,
           static_cast<int>(received),
           reinterpret_cast<const char *>(buffer.data()));
    return OK();
}

inline CommandDesc udpSendCmd = {
    .name = "udp-send",
    .description = "Send a UDP diagnostic packet",
    .args =
        {
            CommandBackend::arg<std::string_view>("host"),
            CommandBackend::arg<uint32_t>("port"),
            CommandBackend::arg<std::string_view>("payload"),
        },
    .handler = handleUdpSend,
    .subcommands = {},
};

inline CommandDesc udpRecvCmd = {
    .name = "udp-recv",
    .description = "Receive one UDP diagnostic packet",
    .args =
        {
            CommandBackend::arg<uint32_t>("port"),
            CommandBackend::arg<uint32_t>(
                "timeoutMs", CommandDesc::ArgRequirement::Optional),
        },
    .handler = handleUdpRecv,
    .subcommands = {},
};

inline CommandDesc udpExchangeCmd = {
    .name = "udp-exchange",
    .description = "Send a UDP diagnostic packet and receive one response",
    .args =
        {
            CommandBackend::arg<std::string_view>("host"),
            CommandBackend::arg<uint32_t>("port"),
            CommandBackend::arg<std::string_view>("payload"),
            CommandBackend::arg<uint32_t>(
                "timeoutMs", CommandDesc::ArgRequirement::Optional),
            CommandBackend::arg<uint32_t>(
                "bindPort", CommandDesc::ArgRequirement::Optional),
        },
    .handler = handleUdpExchange,
    .subcommands = {},
};

inline CommandDesc tcpConnectCmd = {
    .name = "tcp-connect",
    .description = "Connect, send, and receive one TCP diagnostic payload",
    .args =
        {
            CommandBackend::arg<std::string_view>("host"),
            CommandBackend::arg<uint32_t>("port"),
            CommandBackend::arg<std::string_view>("payload"),
            CommandBackend::arg<uint32_t>(
                "timeoutMs", CommandDesc::ArgRequirement::Optional),
        },
    .handler = handleTcpConnect,
    .subcommands = {},
};

inline CommandDesc tcpListenCmd = {
    .name = "tcp-listen",
    .description = "Accept and echo one TCP diagnostic payload",
    .args =
        {
            CommandBackend::arg<uint32_t>("port"),
            CommandBackend::arg<uint32_t>(
                "timeoutMs", CommandDesc::ArgRequirement::Optional),
        },
    .handler = handleTcpListen,
    .subcommands = {},
};

inline ReturnCode registerCommands() {
    auto &registrar = CommandRegistrarService::get();
    FAIL_IF_UNEXPECTED_FWD(udpSendKey,
                           registrar.registerCommand(udpSendCmd),
                           "Failed to register /udp-send command");
    (void)udpSendKey;
    FAIL_IF_UNEXPECTED_FWD(udpRecvKey,
                           registrar.registerCommand(udpRecvCmd),
                           "Failed to register /udp-recv command");
    (void)udpRecvKey;
    FAIL_IF_UNEXPECTED_FWD(udpExchangeKey,
                           registrar.registerCommand(udpExchangeCmd),
                           "Failed to register /udp-exchange command");
    (void)udpExchangeKey;
    FAIL_IF_UNEXPECTED_FWD(tcpConnectKey,
                           registrar.registerCommand(tcpConnectCmd),
                           "Failed to register /tcp-connect command");
    (void)tcpConnectKey;
    FAIL_IF_UNEXPECTED_FWD(tcpListenKey,
                           registrar.registerCommand(tcpListenCmd),
                           "Failed to register /tcp-listen command");
    (void)tcpListenKey;
    return OK();
}

} // namespace Totem::Network::detail
