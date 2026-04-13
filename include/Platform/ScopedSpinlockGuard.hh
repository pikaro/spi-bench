#pragma once

#include "Platform/Platform.hh"

namespace platform {

class ScopedSpinlockGuard {
  public:
    explicit ScopedSpinlockGuard(Spinlock &lock) : _lock(lock) {
        start_critical_section(_lock);
    }

    ScopedSpinlockGuard(const ScopedSpinlockGuard &) = delete;
    ScopedSpinlockGuard &operator=(const ScopedSpinlockGuard &) = delete;

    ~ScopedSpinlockGuard() { end_critical_section(_lock); }

  private:
    Spinlock &_lock;
};

} // namespace platform
