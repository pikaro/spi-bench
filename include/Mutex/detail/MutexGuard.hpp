#pragma once

#include "Macros/Facade.hpp"
#include "PlatformSelect.hpp"

namespace Totem::Mutex::detail {

class MutexGuard {
  public:
    MutexGuard(::platform::MutexHandle mtx,
               ::platform::Tick timeout = TICK_MAX_DELAY, bool critical = true)
        : _mtx{mtx}, _locked(Platform::take_mutex(mtx, timeout).ok()) {
        ABORT_IF(!_locked && critical,
                 "Failed to take mutex in %s within timeout", __func__);
    }

    ~MutexGuard() {
        if (_locked) {
            FAIL_IF_ERR_VOID(Platform::give_mutex(_mtx),
                             "Failed to give mutex in %s", __func__);
        }
    }
    [[nodiscard]] bool locked() const { return _locked; }

  private:
    ::platform::MutexHandle _mtx{};
    bool _locked{};
};

} // namespace Totem::Mutex::detail
