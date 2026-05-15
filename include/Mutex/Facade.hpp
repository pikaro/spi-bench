#pragma once

#include "Mutex/detail/Mutex.hpp"
#include "Mutex/detail/ScopedMutexGuard.hpp"
#include "Mutex/detail/ScopedSpinlockGuard.hpp"

namespace Totem::Mutex {

using detail::Mutex;
using detail::MutexAllocation;
using detail::ScopedMutexGuard;
using detail::ScopedSpinlockGuard;

} // namespace Totem::Mutex
