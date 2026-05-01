#pragma once

#include "Generic/Directory.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "MetricsBackend/detail/MetricGroupDirectory.hpp"
#include "MetricsBackend/detail/Types.hpp"
#include "StaticConfig/Metrics.hpp"
#include "Types/Error.hpp"
#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <expected>

namespace Totem::MetricsBackend::detail {

struct MetricSlot {
    const MetricDesc *desc{};
    MetricGroupKey groupKey{};
    std::atomic<uint32_t> value{0};

    MetricSlot() = default;

    MetricSlot(const MetricDesc *metricDesc, MetricGroupKey metricGroupKey,
               uint32_t metricValue = 0) noexcept
        : desc(metricDesc), groupKey(metricGroupKey), value(metricValue) {}

    MetricSlot(const MetricSlot &other) noexcept
        : desc(other.desc), groupKey(other.groupKey),
          value(other.loadValue()) {}

    MetricSlot &operator=(const MetricSlot &other) noexcept {
        if (this == &other) {
            return *this;
        }
        desc = other.desc;
        groupKey = other.groupKey;
        value.store(other.loadValue(), std::memory_order_relaxed);
        return *this;
    }

    MetricSlot(MetricSlot &&other) noexcept : MetricSlot(other) {}

    MetricSlot &operator=(MetricSlot &&other) noexcept {
        return operator=(other);
    }

    [[nodiscard]] uint32_t loadValue() const noexcept {
        return value.load(std::memory_order_relaxed);
    }

    void increment(uint32_t amount) noexcept {
        (void)value.fetch_add(amount, std::memory_order_relaxed);
    }

    void decrement(uint32_t amount) noexcept {
        (void)value.fetch_sub(amount, std::memory_order_relaxed);
    }

    void set(uint32_t metricValue) noexcept {
        value.store(metricValue, std::memory_order_relaxed);
    }
};

class MetricDirectory;

using MetricDirectoryImpl =
    BaseGettableDirectory<MetricDirectory, MetricKey, MetricSlot,
                          MetricConfig::maxMetrics>;

class MetricDirectory : public MetricDirectoryImpl {
  public:
    static constexpr LogComponent logComponent =
        Totem::MetricsBackend::detail::logComponent;

    explicit MetricDirectory(MetricGroupDirectory &groupDirectory)
        : MetricDirectoryImpl("MetricDirectory"),
          _groupDirectory(groupDirectory) {}

    std::expected<MetricKey, ReturnCode> add(MetricKey metricKey,
                                             MetricGroupKey groupKey,
                                             const MetricDesc &metricDesc) {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            key,
            _addImpl(metricKey, MetricSlot{&metricDesc, groupKey}),
            "Failed to add metric with key %" PRIuPTR, metricKey);
        FAIL_IF_ERR_FWD_UNEXPECTED(_groupDirectory.addMetricToGroup(groupKey),
                                   "Failed to add metric with key %" PRIuPTR
                                   " to group with key %" PRIuPTR,
                                   metricKey, groupKey);
        return key;
    }

    template <typename Fn, typename Filter>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const MetricKey &,
                                       const Metric &>)
    ReturnCode withAllInGroupConst(Fn &&fn,
                                   const MetricGroupKey &groupKey) const {
        return withAllConst(
            [&](const MetricKey &key, const MetricSlot &metricSlot) {
                return fn(key,
                          {.desc = metricSlot.desc,
                           .value = metricSlot.loadValue()});
            },
            [&](const MetricKey & /*unused*/, const MetricSlot &metric) {
                return metric.groupKey == groupKey;
            });
    }

  private:
    MetricGroupDirectory &_groupDirectory;
};

} // namespace Totem::MetricsBackend::detail
