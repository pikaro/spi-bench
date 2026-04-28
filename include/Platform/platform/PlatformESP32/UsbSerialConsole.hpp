#pragma once

#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <fcntl.h>
#include <optional>
#include <span>
#include <stdio.h>
#include <sys/_default_fcntl.h>
#include <sys/unistd.h>
#include <unistd.h>

namespace platform {

struct Console {
    static ReturnCode init() {
        auto flags = fcntl(fileno(stdin), F_GETFL, 0);
        FAIL_IF(flags < 0, ERR(OperationFailed),
                "Failed to get USB serial console input flags");
        FAIL_IF(fcntl(fileno(stdin), F_SETFL, flags | O_NONBLOCK) < 0,
                ERR(OperationFailed),
                "Failed to set USB serial console input nonblocking");
        return OK();
    }

    static ReturnCode write(std::span<const std::byte> data, bool drain = false,
                            std::optional<uint32_t> /*unused*/ = std::nullopt) {
        auto written = fwrite(reinterpret_cast<const char *>(data.data()), 1,
                              data.size(), stdout);
        FAIL_IF(written != data.size(), ERR(OperationFailed),
                "Failed to write to console");

        if (drain) {
            auto ret = fflush(stdout);
            FAIL_IF(ret != 0, ERR(OperationFailed), "Failed to flush console");
        }
        return OK();
    }

    static std::expected<size_t, ReturnCode>
    read(std::span<std::byte> buffer,
         std::optional<uint32_t> /*unused*/ = std::nullopt) {
        if (buffer.empty()) {
            return std::unexpected(ERR(InvalidArgument));
        }

        auto ret =
            ::read(fileno(stdin), reinterpret_cast<char *>(buffer.data()),
                   buffer.size());
        if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return std::unexpected(ERR(NotFound));
            }
            return std::unexpected(ERR(OperationFailed));
        }

        if (ret == 0) {
            return std::unexpected(ERR(NotFound));
        }

        return static_cast<size_t>(ret);
    }

    static ReturnCode deinit() {
        // No deinitialization needed for USB serial console
        return OK();
    }
};

} // namespace platform
