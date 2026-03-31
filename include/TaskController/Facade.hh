#pragma once

#include "Base/HasLifecycle.hh"
#include "detail/Config.hh"
#include "detail/Controller.hh"

namespace Totem::TaskController {

using Controller = detail::Controller;
using Config = detail::Config;
using TaskHooks = detail::TaskHooks;
using RegistryHooks = detail::RegistryHooks;

template <class T, typename ConfT>
struct Contract : public LifecycleContract<T, ConfT> {};

} // namespace Totem::TaskController
