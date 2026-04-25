#pragma once

#include "TaskController/detail/PlatformSelect.hpp"
#include <cstdint>

namespace Totem::TaskController::detail {

class StepScheduler {
  public:
    explicit StepScheduler(uint32_t intervalMs, bool noCatchup)
        : _intervalTicks(::platform::ms_to_ticks(intervalMs)),
          _lastStepTick(::platform::get_tick()), _noCatchup(noCatchup) {}

    void wait() {
        if (_noCatchup) {
            const auto now = ::platform::get_tick();
            if ((now - _lastStepTick) > _intervalTicks) {
                _lastStepTick = now;
            }
        }

        if (_intervalTicks > 0) {
            Platform::delay_task_until(&_lastStepTick, _intervalTicks);
        }
    }

    [[nodiscard]] uint32_t timeoutMsUntilNextStep() {
        return ::platform::ticks_to_ms(_timeoutTicksUntilNextStep());
    }

    void markPeriodicStep() {
        if (_intervalTicks == 0) {
            return;
        }

        const auto now = ::platform::get_tick();
        if (_noCatchup && (now - _lastStepTick) > _intervalTicks) {
            _lastStepTick = now;
            return;
        }

        _lastStepTick += _intervalTicks;
    }

  private:
    [[nodiscard]] ::platform::Tick _timeoutTicksUntilNextStep() {
        if (_intervalTicks == 0) {
            return 0;
        }

        const auto now = ::platform::get_tick();
        if (_noCatchup && (now - _lastStepTick) > _intervalTicks) {
            _lastStepTick = now;
            return _intervalTicks;
        }

        const auto nextStepTick = _lastStepTick + _intervalTicks;
        if (now >= nextStepTick) {
            return 0;
        }
        return nextStepTick - now;
    }

    ::platform::Tick _intervalTicks;
    ::platform::Tick _lastStepTick;
    bool _noCatchup;
};

} // namespace Totem::TaskController::detail
