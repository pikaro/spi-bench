#pragma once

#include "Common.hh"

#include "Concepts/Base.hh"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace Totem::Core {

template <class Derived> class ScopedMutexGuard final {
  public:
    ScopedMutexGuard(SemaphoreHandle_t handle,
                     TickType_t ticksToWait = portMAX_DELAY) noexcept
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

        if (xPortInIsrContext() != pdFALSE) {
            _log_w("%s: In ISR context, cannot take mutex", _name);
            return;
        }

        _log_d("%s: Mutex take by %s", _name, pcTaskGetName(nullptr));

        if (xSemaphoreTake(_handle, ticksToWait) == pdTRUE) {
            _acquired = true;
        } else {
            _log_w("%s: Failed to take mutex by %s within %u ticks", _name,
                   pcTaskGetName(nullptr), static_cast<unsigned>(ticksToWait));
        }
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
            (void)xSemaphoreGive(_handle);
            _acquired = false;
            const char *name = Derived::name;
            _log_d("%s: Mutex released by %s", name, pcTaskGetName(nullptr));
        }
    }

    SemaphoreHandle_t _handle{nullptr};
    bool _acquired{false};
    const char *_name;
};

} // namespace Totem::Core

using Totem::Core::ScopedMutexGuard;
