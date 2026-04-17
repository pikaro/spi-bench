#pragma once

#include "Mutex/detail/Mutex.hpp"
#include "Mutex/detail/MutexGuard.hpp"
#include "Mutex/detail/ScopedMutexGuard.hpp"
#include "Mutex/detail/ScopedSpinlockGuard.hpp"

namespace Totem::Mutex {

using detail::Mutex;
using detail::MutexGuard;
using detail::ScopedMutexGuard;
using detail::ScopedSpinlockGuard;

using detail::execute_mutex_exec_spec;
using detail::execute_mutex_exec_spec_const;
using detail::make_mutex_exec_spec;
using detail::make_mutex_exec_spec_const;
using detail::MutexExecSpec;
using detail::MutexExecSpecConst;

} // namespace Totem::Mutex
