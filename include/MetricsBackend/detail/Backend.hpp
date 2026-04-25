#pragma once

#include "Base/HasCommands.hpp"
#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/IFrameSink.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "MetricsBackend/detail/Commands.hpp"
#include "MetricsBackend/detail/Recorder.hpp"
#include "MetricsBackend/detail/Registrar.hpp"
#include "MetricsBackend/detail/Store.hpp"
#include "MetricsBackend/detail/Types.hpp" // IWYU pragma: keep
#include "Services/Metrics.hpp"
#include "Types/Error.hpp"
#include <cstddef>

namespace Totem::MetricsBackend::detail {

class Backend : public HasLifecycle<Backend>,
                public HasCommands<Backend, Commands<Backend>>,
                public IMetrics {
    friend class HasLifecycle<Backend>;
    friend struct LifecycleContract<Backend>;

  public:
    static constexpr const char *name = "Metrics::System";

    Backend() : _registrar(_store), _recorder(_store) {}

    [[nodiscard]] Registrar &registrar() { return _registrar; }
    [[nodiscard]] Recorder &recorder() { return _recorder; }

    ReturnCode snapshot(IFrameSink &sink,
                        const char *metricGroupName) override {
        if (metricGroupName == nullptr) {
            FAIL(ERR(InvalidArgument), "Metric group name cannot be null");
        }
        FAIL_IF_UNEXPECTED_FWD(key, _store.getGroupKeyByName(metricGroupName),
                               "Failed to get metric group key for name %s",
                               metricGroupName);
        return _snapshot(sink, key);
    }

    ReturnCode snapshot(IFrameSink &sink) {
        FAIL_IF_UNEXPECTED_FWD(snap, _store.getAllGroupKeys(),
                               "Failed to get metric group keys from store");
        auto ret = OK();
        for (size_t i = 0; i < snap.count; ++i) {
            ret.combine(_snapshot(sink, snap.keys[i]));
        }
        FAIL_IF_ERR_FWD(ret, "Failed to snapshot all metric groups");
        return OK();
    }

  private:
    ReturnCode _onBegin() { return _registerCommands(); }
    ReturnCode _onEnd() { return _deregisterCommands(); }

    ReturnCode _snapshot(IFrameSink &sink, MetricGroupKey key) {
        FAIL_IF_UNEXPECTED_FWD(group, _store.getMetricGroup(key),
                               "Failed to get metric group");

        auto result = _store.getMetricsForGroup(key);
        if (!result.has_value()) {
            FAIL(result.error(), "Failed to get metrics for group " SV_FMT,
                 SV_ARG(group.displayName()));
        }
        auto &metrics = result.value();

        FAIL_IF(metrics.count != group.metricCount, ERR(InvalidData),
                "Metric count mismatch for group " SV_FMT
                ": expected %zu, got %zu",
                SV_ARG(group.displayName()), group.metricCount, metrics.count);

        auto snap = MetricFrame{
            .timestampMs = ::platform::get_time(),
            .group = group,
            .metrics = metrics.metrics,
        };

        FAIL_IF_ERR_FWD(sink.consume(snap),
                        "Failed to consume metric snapshot for group " SV_FMT,
                        SV_ARG(group.displayName()));

        return OK();
    }

    Store _store;
    Registrar _registrar;
    Recorder _recorder;
};

inline constexpr LifecycleContract<Backend> _metrics_lifecycle;
inline constexpr CommandsContract<Backend, Commands<Backend>>
    _metrics_commands_contract;

} // namespace Totem::MetricsBackend::detail
