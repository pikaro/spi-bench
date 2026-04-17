#pragma once

#include "Base/HasLifecycle.hh"
#include "TaskController/detail/Concepts.hh"
#include "TaskController/detail/Controller.hh"

namespace Totem::TaskController {

using detail::Controller;
using detail::IsSnapshotHandler;

template <class T, typename ConfT>
struct Contract : public LifecycleContract<T, ConfT> {};

} // namespace Totem::TaskController
