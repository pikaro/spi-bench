#pragma once

#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <expected>
#include <string_view>

namespace Totem::FileSystem::detail {

inline bool hasNullByte(std::string_view value) {
    return value.find('\0') != std::string_view::npos;
}

inline bool hasParentTraversal(std::string_view path) {
    std::size_t offset = 0;
    while (offset < path.size()) {
        while (offset < path.size() && path[offset] == '/') {
            ++offset;
        }

        const auto segmentStart = offset;
        while (offset < path.size() && path[offset] != '/') {
            ++offset;
        }

        const auto segmentSize = offset - segmentStart;
        if (segmentSize == 2 && path[segmentStart] == '.' &&
            path[segmentStart + 1] == '.') {
            return true;
        }
    }
    return false;
}

inline bool pathStartsWithBase(std::string_view base, std::string_view path) {
    return path == base || (path.size() > base.size() &&
                            path.starts_with(base) && path[base.size()] == '/');
}

template <std::size_t N> struct PathBuffer {
    [[nodiscard]] const char *c_str() const { return data.data(); }
    [[nodiscard]] std::string_view view() const {
        return {data.data(), length};
    }

    std::array<char, N> data{};
    std::size_t length = 0;
};

template <std::size_t N>
std::expected<std::string_view, ReturnCode> copyPath(PathBuffer<N> &out,
                                                     std::string_view path) {
    FAIL_IF(hasNullByte(path), std::unexpected(ERR(CoreError, InvalidArgument)),
            "Path contains embedded null byte");
    FAIL_IF(hasParentTraversal(path),
            std::unexpected(ERR(CoreError, InvalidArgument)),
            "Path contains parent traversal");
    FAIL_IF(path.size() + 1 > N, std::unexpected(ERR(CoreError, InvalidSize)),
            "Path too long: %zu > %zu", path.size(), N - 1);

    if (!path.empty()) {
        std::memcpy(out.data.data(), path.data(), path.size());
    }
    out.data[path.size()] = '\0';
    out.length = path.size();
    return out.view();
}

template <std::size_t N>
std::expected<std::string_view, ReturnCode>
makeFullPath(PathBuffer<N> &out, std::string_view basePath,
             std::string_view logicalPath) {
    FAIL_IF(basePath.empty() || basePath.front() != '/',
            std::unexpected(ERR(CoreError, InvalidArgument)),
            "Filesystem base path must be absolute");
    FAIL_IF(hasNullByte(basePath) || hasNullByte(logicalPath),
            std::unexpected(ERR(CoreError, InvalidArgument)),
            "Filesystem path contains embedded null byte");
    FAIL_IF(hasParentTraversal(basePath) || hasParentTraversal(logicalPath),
            std::unexpected(ERR(CoreError, InvalidArgument)),
            "Filesystem path contains parent traversal");

    if (logicalPath.empty() || logicalPath == "/") {
        return copyPath(out, basePath);
    }
    if (pathStartsWithBase(basePath, logicalPath)) {
        return copyPath(out, logicalPath);
    }

    const bool baseEndsSlash = basePath.back() == '/';
    const bool logicalStartsSlash = logicalPath.front() == '/';
    const bool needsSeparator = !baseEndsSlash && !logicalStartsSlash;
    if (baseEndsSlash && logicalStartsSlash) {
        logicalPath.remove_prefix(1);
    }
    const std::size_t length =
        basePath.size() + logicalPath.size() + (needsSeparator ? 1U : 0U);
    FAIL_IF(length + 1 > N, std::unexpected(ERR(CoreError, InvalidSize)),
            "Full filesystem path too long: %zu > %zu", length, N - 1);

    std::size_t offset = 0;
    std::memcpy(out.data.data(), basePath.data(), basePath.size());
    offset += basePath.size();
    if (needsSeparator) {
        out.data[offset++] = '/';
    }
    std::memcpy(out.data.data() + offset, logicalPath.data(),
                logicalPath.size());
    offset += logicalPath.size();
    out.data[offset] = '\0';
    out.length = offset;
    return out.view();
}

template <std::size_t N>
std::expected<std::string_view, ReturnCode>
makeChildPath(PathBuffer<N> &out, std::string_view parentPath,
              std::string_view childName) {
    FAIL_IF(parentPath.empty(),
            std::unexpected(ERR(CoreError, InvalidArgument)),
            "Parent path cannot be empty");
    FAIL_IF(hasNullByte(parentPath) || hasNullByte(childName),
            std::unexpected(ERR(CoreError, InvalidArgument)),
            "Filesystem path contains embedded null byte");
    FAIL_IF(hasParentTraversal(parentPath) || hasParentTraversal(childName),
            std::unexpected(ERR(CoreError, InvalidArgument)),
            "Filesystem path contains parent traversal");

    const bool parentEndsSlash = parentPath.back() == '/';
    const std::size_t length =
        parentPath.size() + childName.size() + (parentEndsSlash ? 0U : 1U);
    FAIL_IF(length + 1 > N, std::unexpected(ERR(CoreError, InvalidSize)),
            "Child filesystem path too long: %zu > %zu", length, N - 1);

    std::size_t offset = 0;
    std::memcpy(out.data.data(), parentPath.data(), parentPath.size());
    offset += parentPath.size();
    if (!parentEndsSlash) {
        out.data[offset++] = '/';
    }
    std::memcpy(out.data.data() + offset, childName.data(), childName.size());
    offset += childName.size();
    out.data[offset] = '\0';
    out.length = offset;
    return out.view();
}

} // namespace Totem::FileSystem::detail
