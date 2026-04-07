#pragma once

#include "Common.hh"

#include "Concepts/Base.hh"
#include "PlatformSelect.hh"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace Totem::Mutex::detail {

template <class Derived> class ScopedMutexGuard final {
  public:
    ScopedMutexGuard(MutexHandle handle,
                     ::platform::Tick ticksToWait = TICK_MAX_DELAY) noexcept
        : _handle{handle}, _name{Derived::name} {
        static_assert(IsNamedEntity<Derived>,
                      "HasMutex requires Derived to be a named entity");

        if (_handle == nullptr) {
            _log_d("%s: Null mutex handle, skipping take", _name);
            return;
        }

        if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
            _log_w("%s: Scheduler not running, cannot take mutex", _name);
            return;
        }

        if (::platform::in_isr()) {
            _log_w("%s: In ISR context, cannot take mutex", _name);
            return;
        }

        _log_d("%s: Mutex take by %s", _name, pcTaskGetName(nullptr));

        FAIL_IF_ERR_VOID(Platform::take_mutex(_handle, ticksToWait),
                         "Failed to take mutex %s", _name);
        _acquired = true;
    }

    ScopedMutexGuard(ScopedMutexGuard const &) = delete;
    ScopedMutexGuard &operator=(ScopedMutexGuard const &) = delete;

    ScopedMutexGuard(ScopedMutexGuard &&other) noexcept
        : _handle{other._handle}, _acquired{other._acquired} {
        other._handle = nullptr;
        other._acquired = false;
    }

    ScopedMutexGuard &operator=(ScopedMutexGuard &&other) noexcept {
        if (this != &other) {
            release();
            _handle = other._handle;
            _acquired = other._acquired;
            other._handle = nullptr;
            other._acquired = false;
        }
        return *this;
    }

    ~ScopedMutexGuard() { release(); }

    [[nodiscard]] bool acquired() const noexcept { return _acquired; }

  private:
    void release() noexcept {
        if ((_handle != nullptr) && _acquired) {
            (void)Platform::give_mutex(_handle);
            _acquired = false;
            const char *name = Derived::name;
            _log_d("%s: Mutex released by %s", name, pcTaskGetName(nullptr));
        }
    }

    MutexHandle _handle{nullptr};
    bool _acquired{false};
    const char *_name;
};

} // namespace Totem::Mutex::detail
