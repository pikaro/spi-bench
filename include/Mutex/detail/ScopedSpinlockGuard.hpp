#pragma once

#include "Platform/PlatformSelect.hpp"

namespace Totem::Mutex::detail {

class ScopedSpinlockGuard {
  public:
    explicit ScopedSpinlockGuard(::platform::Spinlock &lock) : _lock(lock) {
        ::platform::start_critical_section(_lock);
    }

    ScopedSpinlockGuard(const ScopedSpinlockGuard &) = delete;
    ScopedSpinlockGuard &operator=(const ScopedSpinlockGuard &) = delete;

    ~ScopedSpinlockGuard() { ::platform::end_critical_section(_lock); }

  private:
    ::platform::Spinlock &_lock;
};

} // namespace Totem::Mutex::detail
