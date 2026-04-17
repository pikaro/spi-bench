#pragma once

#include "FreeRTOSConfig.h"
#include "Macros/Facade.hh"
#include "Monitoring/Interfaces/Sink.hh"
#include "Monitoring/detail/Types.hh"
#include "Support/Basic.hh"
#include "Types/Error.hh"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"
#include "freertos/portable.h"
#include "freertos/projdefs.h"
#include "portmacro.h"
#include "sdkconfig.h"
#include <cstddef>
#include <cstdint>
#include <span>

namespace Totem::Monitoring::detail::platform {

struct Platform {
    static constexpr uint8_t MemoryStatCount = 7;

    static ReturnCode collect_memory_stats_into(std::span<MemoryStats> out) {
        if (out.size() < MemoryStatCount) {
            return ERR(CoreError, InvalidArgument);
        }

        out[0] = _get_memory_stats(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, "InternalData",
            to_bits(MemoryAttr::Internal | MemoryAttr::GeneralPurpose),
            to_bits(MemoryStatFlags::Overlapping));

        out[1] = _get_memory_stats(
            MALLOC_CAP_SPIRAM, "ExternalData",
            to_bits(MemoryAttr::External | MemoryAttr::GeneralPurpose),
            to_bits(MemoryStatFlags::Overlapping |
                    MemoryStatFlags::Conditional));

        out[2] = _get_memory_stats(MALLOC_CAP_DMA, "DMA",
                                   to_bits(MemoryAttr::DmaCapable),
                                   to_bits(MemoryStatFlags::Overlapping |
                                           MemoryStatFlags::Specialized));

        out[3] = _get_memory_stats(MALLOC_CAP_DEFAULT, "DefaultAlloc",
                                   to_bits(MemoryAttr::DefaultAlloc),
                                   to_bits(MemoryStatFlags::Overlapping));

        out[4] = _get_memory_stats(MALLOC_CAP_RETENTION, "Retention",
                                   to_bits(MemoryAttr::Retained),
                                   to_bits(MemoryStatFlags::Overlapping |
                                           MemoryStatFlags::Conditional |
                                           MemoryStatFlags::Specialized));

        out[5] = _get_memory_stats(
            MALLOC_CAP_RTCRAM, "RTCRAM",
            to_bits(MemoryAttr::FastRtc | MemoryAttr::Retained),
            to_bits(MemoryStatFlags::Overlapping |
                    MemoryStatFlags::Conditional |
                    MemoryStatFlags::Specialized));

        auto total = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
        auto totalMin = xPortGetMinimumEverFreeHeapSize();
        auto totalFree = xPortGetFreeHeapSize();

        out[6] = MemoryStats{
            .name = "Total",
            .totalBytes = total,
            .freeBytes = totalFree,
            .minFreeBytes = totalMin,
            .freePct = safe_pct(totalFree, total),
            .minFreePct = safe_pct(totalMin, total),
            .attrs = 0,
            .flags = to_bits(MemoryStatFlags::Specialized),
        };

        return OK(CoreError);
    }

    static ReturnCode collect_cpu_free_into(std::span<uint32_t> out) {
        if (out.size() < ::platform::CoreCount) {
            return ERR(CoreError, InvalidArgument);
        }

        for (BaseType_t i = 0; i < ::platform::CoreCount; ++i) {
            auto *taskHandle = xTaskGetIdleTaskHandleForCore(i);
            TaskStatus_t taskStatus;
            vTaskGetInfo(taskHandle, &taskStatus, pdTRUE, eInvalid);
            out[static_cast<size_t>(i)] =
                _runtime_counter_to_ms(taskStatus.ulRunTimeCounter);
        }

        return OK(CoreError);
    }

  private:
    static uint32_t _runtime_counter_to_ms(configRUN_TIME_COUNTER_TYPE value) {
#if CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER
        return static_cast<uint32_t>(value / 1000ULL);
#else
        return static_cast<uint32_t>(value);
#endif
    }

    static MemoryStats _get_memory_stats(size_t kind, const char *regionName,
                                         uint8_t attrs, uint8_t flags) {
        size_t max = heap_caps_get_total_size(kind);
        size_t now = heap_caps_get_free_size(kind);
        size_t min = heap_caps_get_minimum_free_size(kind);

        return MemoryStats{
            .name = regionName,
            .totalBytes = max,
            .freeBytes = now,
            .minFreeBytes = min,
            .freePct = safe_pct(now, max),
            .minFreePct = safe_pct(min, max),
            .attrs = attrs,
            .flags = flags,
        };
    }
};

} // namespace Totem::Monitoring::detail::platform
