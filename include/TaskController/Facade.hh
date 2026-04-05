#pragma once

#include "Base/HasLifecycle.hh"
#include "TaskController/detail/Concepts.hh"
#include "TaskController/detail/Config.hh"
#include "TaskController/detail/Controller.hh"
#include "TaskController/detail/Types.hh"

namespace Totem::TaskController {

using detail::Config;
using detail::Controller;
using detail::exit_reason_to_string;
using detail::IsSnapshotHandler;
using detail::platform_state_to_string;
using detail::RegistryHooks;
using detail::state_to_string;
using detail::TaskHooks;
using detail::TaskRuntimeSnapshot;

template <class T, typename ConfT>
struct Contract : public LifecycleContract<T, ConfT> {};

} // namespace Totem::TaskController
