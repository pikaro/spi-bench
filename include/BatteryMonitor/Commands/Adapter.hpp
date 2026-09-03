#pragma once

#include "Base/HasCommands.hpp"
#include "BatteryMonitor/Facade.hpp"
#include "BatteryMonitor/detail/Commands.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <expected>

namespace Totem::BatteryMonitor::Commands {

/** Explicit, optional registration seam for the global battery CLI. */
class Adapter : public HasCommands<Adapter, detail::Commands<Adapter>> {
  public:
    DELETE_COPY(Adapter)
    DELETE_MOVE(Adapter)

    static constexpr const char *name = "BatteryMonitor::Commands";

    Adapter() = default;

    ReturnCode begin(BatteryMonitor &monitor) {
        FAIL_IF(_active, ERR(LifecycleError, Active),
                "Battery command adapter is already active");
        FAIL_IF(!monitor.active(), ERR(CoreError, InvalidState),
                "Battery monitor must be active before command registration");
        _monitor = &monitor;
        auto ret = _registerCommands();
        if (!ret.ok()) {
            _monitor = nullptr;
            return ret;
        }
        _active = true;
        return OK();
    }

    ReturnCode end() {
        FAIL_IF(!_active, ERR(LifecycleError, NotActive),
                "Battery command adapter is inactive");
        auto ret = _deregisterCommands();
        _active = false;
        _monitor = nullptr;
        return ret;
    }

    [[nodiscard]] bool active() const { return _active; }

    [[nodiscard]] std::expected<BatteryStatus, ReturnCode> status() const {
        if (_monitor == nullptr) {
            return std::unexpected(ERR(CoreError, InvalidState));
        }
        return _monitor->status();
    }

    ReturnCode startCalibration() {
        return _monitor == nullptr ? ERR(CoreError, InvalidState)
                                   : _monitor->startCalibration();
    }

    [[nodiscard]] BatteryCalibrationStartResult requestCalibrationStart() {
        if (_monitor == nullptr) {
            return {
                .error = ERR(CoreError, InvalidState),
                .reason = BatteryCalibrationInvalidReason::None,
            };
        }
        return _monitor->requestCalibrationStart();
    }

    ReturnCode abortCalibration() {
        return _monitor == nullptr ? ERR(CoreError, InvalidState)
                                   : _monitor->abortCalibration();
    }

  private:
    BatteryMonitor *_monitor = nullptr;
    bool _active = false;
};

inline constexpr CommandsContract<Adapter, detail::Commands<Adapter>>
    _battery_monitor_commands_contract;

} // namespace Totem::BatteryMonitor::Commands
