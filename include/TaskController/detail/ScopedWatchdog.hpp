#pragma once

#include "Macros/Facade.hpp"
#include "PlatformSelect.hpp"

namespace Totem::TaskController::detail {

class ScopedWatchdog {
  public:
    ScopedWatchdog(bool enabled) : _enabled(enabled) {
        if (_enabled) {
            auto result = Platform::wdt_add();
            ABORT_IF_ERR(result, "Failed to add task to watchdog");
        }
    }

    ~ScopedWatchdog() {
        if (_enabled) {
            auto result = Platform::wdt_remove();
            ABORT_IF_ERR(result, "Failed to remove task from watchdog");
        }
    }

    void reset() const {
        if (_enabled) {
            Platform::wdt_reset();
        }
    }

  private:
    bool _enabled = true;
};

} // namespace Totem::TaskController::detail
