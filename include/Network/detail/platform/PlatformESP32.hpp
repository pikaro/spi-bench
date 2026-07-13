// IWYU pragma: private

#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Network/Interfaces/Endpoint.hpp"
#include "Platform/PlatformSelect.hpp"
#include "StaticConfig/Network.hpp"
#include "Types/Error.hpp"
#include "esp_heap_caps.h"
#include "lwip/sockets.h"
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <fcntl.h>
#include <span>
#include <string_view>
#include <utility>

namespace Totem::Network::detail::platform {

static constexpr LogComponent logComponent = LogComponent::System;
static constexpr uint8_t udpSendAttempts = 4;
static constexpr uint32_t udpSendRetryDelayMs = 5;

struct ReceiveResult {
    Ipv4Endpoint remote{};
    std::size_t size = 0;
};

struct EndpointText {
    std::array<char, 32> data{};

    [[nodiscard]] const char *c_str() const { return data.data(); }
};

inline ReturnCode mapErrno(int error) {
    switch (error) {
    case 0:
        return OK();
    case EAGAIN:
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
    case ETIMEDOUT:
        return ERR(CoreError, Timeout);
    case ECONNREFUSED:
    case ECONNRESET:
    case ENOTCONN:
        return ERR(CoreError, InvalidState);
    case EADDRINUSE:
        return ERR(CoreError, AlreadyExists);
    case EADDRNOTAVAIL:
    case ENETUNREACH:
    case EHOSTUNREACH:
        return ERR(CoreError, NotFound);
    case EINVAL:
        return ERR(CoreError, InvalidArgument);
    case ENOMEM:
    case ENOBUFS:
        return ERR(CoreError, OutOfMemory);
    default:
        return ERR(CoreError, OperationFailed);
    }
}

inline ReturnCode socketFailure() {
    return errno == 0 ? ERR(CoreError, OperationFailed) : mapErrno(errno);
}

inline sockaddr_in toSockaddr(Ipv4Endpoint endpoint) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = endpoint.address;
    address.sin_port = htons(endpoint.port);
    return address;
}

inline std::expected<Ipv4Endpoint, ReturnCode>
parseIpv4Endpoint(std::string_view host, uint16_t port) {
    FAIL_IF(host.empty() || host.size() > 15,
            std::unexpected(ERR(CoreError, InvalidArgument)),
            "Invalid IPv4 host length");

    std::array<char, 16> hostBuffer{};
    std::memcpy(hostBuffer.data(), host.data(), host.size());

    in_addr parsed{};
    errno = 0;
    const auto parseRet = lwip_inet_pton(AF_INET, hostBuffer.data(), &parsed);
    FAIL_IF(parseRet != 1, std::unexpected(ERR(CoreError, InvalidArgument)),
            "Invalid IPv4 address " SV_FMT, SV_ARG(host));

    return Ipv4Endpoint{.address = parsed.s_addr, .port = port};
}

inline EndpointText formatEndpoint(Ipv4Endpoint endpoint) {
    EndpointText out{};
    std::array<char, 16> ip{};
    in_addr address{.s_addr = endpoint.address};
    if (lwip_inet_ntop(AF_INET, &address, ip.data(), ip.size()) == nullptr) {
        std::snprintf(out.data.data(), out.data.size(), "?:%u",
                      static_cast<unsigned>(endpoint.port));
        return out;
    }
    std::snprintf(out.data.data(), out.data.size(), "%s:%u", ip.data(),
                  static_cast<unsigned>(endpoint.port));
    return out;
}

inline ReturnCode setTimeout(int fd, int option, uint32_t timeoutMs) {
    timeval timeout{};
    timeout.tv_sec = static_cast<long>(timeoutMs / 1000U);
    timeout.tv_usec = static_cast<long>((timeoutMs % 1000U) * 1000U);
    errno = 0;
    const auto ret =
        lwip_setsockopt(fd, SOL_SOCKET, option, &timeout, sizeof(timeout));
    FAIL_IF(ret != 0, socketFailure(), "Failed to set socket timeout");
    return OK();
}

inline ReturnCode closeFd(int &fd) {
    if (fd < 0) {
        return OK();
    }
    const auto closing = std::exchange(fd, -1);
    errno = 0;
    const auto ret = lwip_close(closing);
    FAIL_IF(ret != 0, socketFailure(), "Failed to close socket");
    return OK();
}

class UdpSocket {
  public:
    DELETE_COPY(UdpSocket)

    UdpSocket() = default;
    ~UdpSocket() { (void)close(); }

    UdpSocket(UdpSocket &&other) noexcept { *this = std::move(other); }
    UdpSocket &operator=(UdpSocket &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        (void)close();
        _fd = std::exchange(other._fd, -1);
        return *this;
    }

    ReturnCode open(uint16_t bindPort = 0) {
        FAIL_IF(_fd >= 0, ERR(CoreError, InvalidState),
                "UDP socket already open");
        errno = 0;
        _fd = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        FAIL_IF(_fd < 0, socketFailure(), "Failed to create UDP socket");

        if (bindPort == 0) {
            return OK();
        }

        int reuse = 1;
        (void)lwip_setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
                              sizeof(reuse));

        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(bindPort);
        errno = 0;
        const auto ret =
            lwip_bind(_fd, reinterpret_cast<const sockaddr *>(&local),
                      sizeof(local));
        if (ret != 0) {
            const auto err = socketFailure();
            (void)close();
            FAIL_ERR_FWD(err, "Failed to bind UDP socket on port %u",
                         static_cast<unsigned>(bindPort));
        }
        return OK();
    }

    ReturnCode close() { return closeFd(_fd); }

    ReturnCode sendTo(Ipv4Endpoint endpoint,
                      std::span<const std::byte> data) {
        FAIL_IF(_fd < 0, ERR(CoreError, InvalidState),
                "UDP socket is not open");
        FAIL_IF(data.empty(), ERR(CoreError, InvalidArgument),
                "Cannot send empty UDP packet");

        const auto remote = toSockaddr(endpoint);
        int failureErrno = 0;
        ssize_t sent = -1;
        for (uint8_t attempt = 0; attempt < udpSendAttempts; ++attempt) {
            errno = 0;
            sent = lwip_sendto(
                _fd, data.data(), data.size(), 0,
                reinterpret_cast<const sockaddr *>(&remote), sizeof(remote));
            if (sent >= 0) {
                break;
            }
            failureErrno = errno;
            if ((failureErrno != ENOMEM && failureErrno != ENOBUFS) ||
                attempt + 1U >= udpSendAttempts) {
                break;
            }
            ::platform::delay(::platform::ms_to_ticks(udpSendRetryDelayMs));
        }
        if (sent < 0) {
            _log_w("UDPtx fail fd=%d n=%zu e=%d def=%zu/%zu int=%zu/%zu dma=%zu/%zu",
                   _fd, data.size(), failureErrno,
                   heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                   heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT),
                   heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                   heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                   heap_caps_get_free_size(MALLOC_CAP_DMA),
                   heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
            errno = failureErrno;
        }
        FAIL_IF(sent < 0, socketFailure(), "Failed to send UDP packet");
        FAIL_IF(static_cast<std::size_t>(sent) != data.size(),
                ERR(CoreError, InvalidSize), "Partial UDP send");
        return OK();
    }

    ReturnCode receiveFrom(std::span<std::byte> out, uint32_t timeoutMs,
                           ReceiveResult &result) {
        if (_fd < 0) {
            return ERR(CoreError, InvalidState);
        }
        if (out.empty()) {
            return ERR(CoreError, InvalidArgument);
        }
        const auto timeoutRet = setTimeout(_fd, SO_RCVTIMEO, timeoutMs);
        if (!timeoutRet.ok()) {
            return timeoutRet;
        }

        sockaddr_in remote{};
        socklen_t remoteLength = sizeof(remote);
        errno = 0;
        const auto received = lwip_recvfrom(
            _fd, out.data(), out.size(), 0,
            reinterpret_cast<sockaddr *>(&remote), &remoteLength);
        if (received < 0) {
            return socketFailure();
        }

        result = {};
        result.remote.address = remote.sin_addr.s_addr;
        result.remote.port = ntohs(remote.sin_port);
        result.size = static_cast<std::size_t>(received);
        return OK();
    }

  private:
    int _fd = -1;
};

class TcpConnection {
  public:
    DELETE_COPY(TcpConnection)

    TcpConnection() = default;
    explicit TcpConnection(int fd) : _fd(fd) {}
    ~TcpConnection() { (void)close(); }

    TcpConnection(TcpConnection &&other) noexcept { *this = std::move(other); }
    TcpConnection &operator=(TcpConnection &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        (void)close();
        _fd = std::exchange(other._fd, -1);
        return *this;
    }

    ReturnCode close() { return closeFd(_fd); }

    ReturnCode sendAll(std::span<const std::byte> data) {
        FAIL_IF(_fd < 0, ERR(CoreError, InvalidState),
                "TCP connection is not open");
        FAIL_IF(data.empty(), ERR(CoreError, InvalidArgument),
                "Cannot send empty TCP payload");

        std::size_t offset = 0;
        while (offset < data.size()) {
            errno = 0;
            const auto sent = lwip_send(_fd, data.data() + offset,
                                        data.size() - offset, 0);
            FAIL_IF(sent < 0, socketFailure(), "Failed to send TCP payload");
            FAIL_IF(sent == 0, ERR(CoreError, InvalidState),
                    "TCP send returned zero bytes");
            offset += static_cast<std::size_t>(sent);
        }
        return OK();
    }

    ReturnCode receive(std::span<std::byte> out, uint32_t timeoutMs,
                       std::size_t &receivedBytes) {
        FAIL_IF(_fd < 0, ERR(CoreError, InvalidState),
                "TCP connection is not open");
        FAIL_IF(out.empty(), ERR(CoreError, InvalidArgument),
                "TCP receive buffer is empty");
        FAIL_IF_ERR_FWD(setTimeout(_fd, SO_RCVTIMEO, timeoutMs),
                        "Failed to set TCP receive timeout");

        errno = 0;
        const auto received = lwip_recv(_fd, out.data(), out.size(), 0);
        FAIL_IF(received < 0, socketFailure(),
                "Failed to receive TCP payload");
        FAIL_IF(received == 0, ERR(CoreError, InvalidState),
                "TCP peer closed before payload");
        receivedBytes = static_cast<std::size_t>(received);
        return OK();
    }

  private:
    int _fd = -1;
};

class TcpClient {
  public:
    DELETE_COPY(TcpClient)
    DELETE_MOVE(TcpClient)

    TcpClient() = default;

    ReturnCode connectTo(Ipv4Endpoint endpoint, uint32_t timeoutMs,
                         TcpConnection &connection) {
        errno = 0;
        auto fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        FAIL_IF(fd < 0, socketFailure(), "Failed to create TCP socket");

        auto cleanup = [&fd] {
            if (fd >= 0) {
                (void)lwip_close(std::exchange(fd, -1));
            }
        };

        if (const auto ret = setTimeout(fd, SO_RCVTIMEO, timeoutMs);
            !ret.ok()) {
            cleanup();
            return ret;
        }
        if (const auto ret = setTimeout(fd, SO_SNDTIMEO, timeoutMs);
            !ret.ok()) {
            cleanup();
            return ret;
        }

        const auto flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }

        const auto remote = toSockaddr(endpoint);
        errno = 0;
        auto connectRet =
            lwip_connect(fd, reinterpret_cast<const sockaddr *>(&remote),
                         sizeof(remote));
        if (connectRet != 0 && errno == EINPROGRESS && timeoutMs > 0) {
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(fd, &writeSet);
            timeval timeout{};
            timeout.tv_sec = static_cast<long>(timeoutMs / 1000U);
            timeout.tv_usec = static_cast<long>((timeoutMs % 1000U) * 1000U);
            errno = 0;
            const auto selectRet =
                lwip_select(fd + 1, nullptr, &writeSet, nullptr, &timeout);
            if (selectRet > 0) {
                int socketError = 0;
                socklen_t socketErrorLength = sizeof(socketError);
                errno = 0;
                const auto optRet = lwip_getsockopt(
                    fd, SOL_SOCKET, SO_ERROR, &socketError,
                    &socketErrorLength);
                if (optRet != 0 || socketError != 0) {
                    const auto err =
                        optRet != 0 ? socketFailure() : mapErrno(socketError);
                    cleanup();
                    _log_e("TCP connect failed: " ERR_FMT, ERR_ARG(err));
                    return err;
                }
                connectRet = 0;
            } else if (selectRet == 0) {
                cleanup();
                return ERR(CoreError, Timeout);
            } else {
                const auto err = socketFailure();
                cleanup();
                return err;
            }
        }

        if (flags >= 0) {
            (void)fcntl(fd, F_SETFL, flags);
        }

        if (connectRet == 0) {
            connection = TcpConnection{std::exchange(fd, -1)};
            return OK();
        }

        const auto err = socketFailure();
        cleanup();
        _log_e("Failed to connect TCP socket: " ERR_FMT, ERR_ARG(err));
        return err;
    }
};

class TcpListener {
  public:
    DELETE_COPY(TcpListener)

    TcpListener() = default;
    ~TcpListener() { (void)close(); }

    TcpListener(TcpListener &&other) noexcept { *this = std::move(other); }
    TcpListener &operator=(TcpListener &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        (void)close();
        _fd = std::exchange(other._fd, -1);
        return *this;
    }

    ReturnCode open(uint16_t port) {
        FAIL_IF(_fd >= 0, ERR(CoreError, InvalidState),
                "TCP listener already open");
        errno = 0;
        _fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        FAIL_IF(_fd < 0, socketFailure(), "Failed to create TCP listener");

        int reuse = 1;
        (void)lwip_setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
                              sizeof(reuse));

        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(port);
        errno = 0;
        auto ret =
            lwip_bind(_fd, reinterpret_cast<const sockaddr *>(&local),
                      sizeof(local));
        if (ret != 0) {
            const auto err = socketFailure();
            (void)close();
            FAIL_ERR_FWD(err, "Failed to bind TCP listener on port %u",
                         static_cast<unsigned>(port));
        }

        errno = 0;
        ret = lwip_listen(_fd, Totem::StaticConfig::Network::tcpListenBacklog);
        if (ret != 0) {
            const auto err = socketFailure();
            (void)close();
            FAIL_ERR_FWD(err, "Failed to listen on TCP port %u",
                         static_cast<unsigned>(port));
        }
        return OK();
    }

    ReturnCode close() { return closeFd(_fd); }

    ReturnCode acceptOne(uint32_t timeoutMs, TcpConnection &connection) {
        FAIL_IF(_fd < 0, ERR(CoreError, InvalidState),
                "TCP listener is not open");

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(_fd, &readSet);
        timeval timeout{};
        timeout.tv_sec = static_cast<long>(timeoutMs / 1000U);
        timeout.tv_usec = static_cast<long>((timeoutMs % 1000U) * 1000U);
        errno = 0;
        const auto selectRet =
            lwip_select(_fd + 1, &readSet, nullptr, nullptr, &timeout);
        FAIL_IF(selectRet < 0, socketFailure(),
                "Failed while waiting for TCP client");
        FAIL_IF(selectRet == 0, ERR(CoreError, Timeout),
                "Timed out waiting for TCP client");

        sockaddr_in remote{};
        socklen_t remoteLength = sizeof(remote);
        errno = 0;
        const auto clientFd = lwip_accept(
            _fd, reinterpret_cast<sockaddr *>(&remote), &remoteLength);
        FAIL_IF(clientFd < 0, socketFailure(),
                "Failed to accept TCP client");

        connection = TcpConnection{clientFd};
        return OK();
    }

  private:
    int _fd = -1;
};

} // namespace Totem::Network::detail::platform
