#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <system_error>

inline size_t cstr_len(const char *str) {
    size_t num = 0;
    while (str[num] != '\0') {
        ++num;
    }
    return num;
}

template <size_t N> inline size_t array_strnlen(const std::array<char, N> &a) {
    size_t num = 0;
    while (num < N && a[num] != '\0') {
        ++num;
    }
    return num;
}

struct BufferWriter {
    char *cur;
    char *end;

    bool append_char(char chr) {
        if (cur == end) {
            return false;
        }
        *cur++ = chr;
        return true;
    }

    bool append_str(const char *str, size_t len) {
        if (static_cast<size_t>(end - cur) < len) {
            return false;
        }
        std::memcpy(cur, str, len);
        cur += len;
        return true;
    }

    template <size_t N> bool append_array_string(const std::array<char, N> &a) {
        return append_str(a.data(), array_strnlen(a));
    }

    bool append_u32(uint32_t value) {
        auto res = std::to_chars(cur, end, value);
        if (res.ec != std::errc{}) {
            return false;
        }
        cur = res.ptr;
        return true;
    }

    [[nodiscard]] size_t size() const {
        return static_cast<size_t>(cur - (end - capacity()));
    }

  private:
    [[nodiscard]] ptrdiff_t capacity() const { return end - cur; }
};
