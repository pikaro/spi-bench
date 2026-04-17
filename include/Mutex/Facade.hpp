#pragma once

#include "Mutex/detail/Mutex.hh"
#include "Mutex/detail/MutexGuard.hh"
#include "Mutex/detail/ScopedMutexGuard.hh"
#include "Mutex/detail/ScopedSpinlockGuard.hh"

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
