#pragma once

#include "Clock/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <cstdint>

struct ClockSyncSetupConfig {
    uint32_t resyncIntervalMs = 10000;
};

template <class Link> class ClockSyncSetup {
  public:
    ClockSyncSetup(Totem::Clock::Clock &clock, Link &link,
                   ClockSyncSetupConfig config = {})
        : _clock(clock), _link(link), _config(config) {}

    ReturnCode work(uint32_t nowMs) {
        if (!_link.ready() || _clock.syncing()) {
            return OK();
        }

        if (_clock.synced() && _lastSyncAttemptAtMs != 0 &&
            nowMs - _lastSyncAttemptAtMs < _config.resyncIntervalMs) {
            return OK();
        }

        _lastSyncAttemptAtMs = nowMs;
        const auto syncResult = _clock.sync(_link);
        if (!syncResult.ok()) {
            _log_e("Clock sync request failed: " ERR_FMT,
                   ERR_ARG(syncResult));
            return OK();
        }

        _log_i("Clock sync requested");
        return OK();
    }

  private:
    Totem::Clock::Clock &_clock;
    Link &_link;
    ClockSyncSetupConfig _config;
    uint32_t _lastSyncAttemptAtMs = 0;
};
