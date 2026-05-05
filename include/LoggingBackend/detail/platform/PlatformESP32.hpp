// IWYU pragma: private

#pragma once

#include "esp_log_level.h"
#include "esp_log_write.h"
#include <cstdio>

namespace Totem::LoggingBackend::detail::platform {

struct Platform {
    using NativeLogVprintf = vprintf_like_t;
    using NativeLogLevel = esp_log_level_t;

    static NativeLogVprintf setNativeLogVprintf(NativeLogVprintf func) {
        return esp_log_set_vprintf(func);
    }

    static NativeLogVprintf defaultNativeLogVprintf() { return &::vprintf; }

    static void limitNativeLogLevel(NativeLogLevel level) {
        if (esp_log_get_default_level() > level) {
            esp_log_level_set("*", level);
        }
    }
};

} // namespace Totem::LoggingBackend::detail::platform
