#pragma once

#include "Traits/Bitmask.hh"
#include "Types/Logging.hh"
#include <cstdint>
#include <type_traits>

namespace Totem::Monitoring::detail {

inline constexpr LogComponent logComponent = LogComponent::Monitoring;

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

} // namespace Totem::Monitoring::detail

template <>
struct BitmaskTrait<Totem::Monitoring::detail::MemoryAttr> : std::true_type {};

template <>
struct BitmaskTrait<Totem::Monitoring::detail::MemoryStatFlags>
    : std::true_type {};
