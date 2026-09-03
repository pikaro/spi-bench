#pragma once

#include "Types/Error.hpp"

namespace MasterDebugMode {

// State machine: transition hooks run once after each successful state change.
template <ReturnCode (*OnStart)(), ReturnCode (*OnStop)()> class Mode {
  public:
    [[nodiscard]] bool active() const { return _active; }

    ReturnCode start() { return setActive(true); }
    ReturnCode stop() { return setActive(false); }
    ReturnCode toggle() { return setActive(!_active); }

  private:
    ReturnCode setActive(bool active) {
        if (_active == active) {
            return ReturnCode::from(CoreError::Ok);
        }

        const auto result = active ? OnStart() : OnStop();
        if (!result.ok()) {
            return result;
        }
        _active = active;
        return ReturnCode::from(CoreError::Ok);
    }

    bool _active = false;
};

} // namespace MasterDebugMode
