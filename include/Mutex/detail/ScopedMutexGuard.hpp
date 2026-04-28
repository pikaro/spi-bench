#pragma once

#include "Concepts/Base.hpp"
#include "Macros/Facade.hpp"
#include "Mutex/detail/Metrics.hpp"
#include "PlatformSelect.hpp"

namespace Totem::Mutex::detail {

template <class Derived> class ScopedMutexGuard final {
  public:
    ScopedMutexGuard(MutexHandle handle,
                     ::platform::Tick ticksToWait = TICK_MAX_DELAY) noexcept
        : _handle{handle}, _name{Derived::name} {
        static_assert(IsNamedEntity<Derived>,
                      "HasMutex requires Derived to be a named entity");

        if (!::platform::is_multithreading()) {
            // If not multithreading, no need to take mutex
            _log_d("%s: Not in multithreading context, skipping mutex take",
                   _name);
            _acquired = true;
            return;
        }

        if (!Platform::can_take_mutex(_name, _handle)) {
            // NOTE: Can NOT record failure here! Recording a failure metric
            //       requires taking the mutex
            FAIL_VOID("Mutex %s cannot be taken by %s", _name,
                      ::platform::current_task_name());
        }

        auto takeResult = Platform::take_mutex(_handle, ticksToWait);
        if (!takeResult) {
            if (takeResult == ERR(Timeout)) {
                _recordTimeout();
                FAIL_VOID("Timed out taking mutex %s", _name);
            }
            _recordFailure();
            FAIL_VOID("Failed to take mutex %s: " ERR_FMT, _name,
                      ERR_ARG(takeResult));
        }

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
        }
    }

    void _recordTimeout() const noexcept {
        // Only record metrics if scheduler is running to avoid circular
        // dependency during static initialization
        if (::platform::is_multithreading()) {
            if (!metrics().timeout()) {
                _log_e("Failed to record mutex timeout metric for %s", _name);
            }
        }
    }

    void _recordFailure() const noexcept {
        // Only record metrics if scheduler is running to avoid circular
        // dependency during static initialization
        if (::platform::is_multithreading()) {
            if (!metrics().failure()) {
                _log_e("Failed to record mutex failure metric for %s", _name);
            }
        }
    }

    MutexHandle _handle{nullptr};
    bool _acquired{false};
    const char *_name;
};

} // namespace Totem::Mutex::detail
