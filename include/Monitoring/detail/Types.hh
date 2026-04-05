#pragma once

#include "TaskController/Facade.hh"
#include "Traits/Bitmask.hh"
#include "Types/Error.hh"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace Totem::Monitoring::detail {

enum class MemoryAttr : uint8_t {
    None = 0,
    Internal = 1U << 0,       // on-chip / non-external
    External = 1U << 1,       // off-chip / PSRAM-style
    GeneralPurpose = 1U << 2, // normal byte-addressable data heap
    DefaultAlloc = 1U << 3,   // eligible for plain malloc()
    DmaCapable = 1U << 4,     // suitable for DMA buffers
    Retained = 1U << 5, // retained across relevant low-power/reset boundary
    FastRtc = 1U << 6,  // RTC fast memory / similar special class
};

enum class MemoryStatFlags : uint8_t {
    None = 0,
    Overlapping = 1U << 0, // overlaps other reported views
    Conditional = 1U << 1, // only exists on some SKUs / configs
    Specialized = 1U << 2, // niche/special-purpose rather than general heap
};

struct MemoryStats {
    std::string_view name;
    size_t totalBytes;
    size_t freeBytes;
    size_t minFreeBytes;
    float freePct;
    float minFreePct;
    uint32_t attrs;
    uint32_t flags;
};

struct GlobalMonitoringSnapshot {
    uint8_t taskCount = 0;
    std::span<uint32_t> coreIdleTimeTotalMs;
    std::span<uint32_t> coreIdleTimeDeltaMs;
    std::span<float> coreUtilizationPctTotal;
    std::span<float> coreUtilizationPctDelta;
    std::span<MemoryStats> memoryStats;
};

struct MonitoringFrame {
    uint32_t timestamp = 0;
    GlobalMonitoringSnapshot global;
    std::span<const TaskController::TaskRuntimeSnapshot> tasks;
};

struct MonitoringSink {
    void *self = nullptr;

    ReturnCode (*consumeHook)(void *, const MonitoringFrame &) = nullptr;

    ReturnCode consume(const MonitoringFrame &frame) const {
        return consumeHook(self, frame);
    }

    [[nodiscard]] bool validate() const {
        return self != nullptr && consumeHook != nullptr;
    }
};

} // namespace Totem::Monitoring::detail

template <>
struct BitmaskTrait<Totem::Monitoring::detail::MemoryAttr> : std::true_type {};

template <>
struct BitmaskTrait<Totem::Monitoring::detail::MemoryStatFlags>
    : std::true_type {};
