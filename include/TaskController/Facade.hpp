#pragma once

#include "Base/HasLifecycle.hpp"
#include "TaskController/detail/Concepts.hpp"
#include "TaskController/detail/Controller.hpp"

namespace Totem::TaskController {

using detail::Controller;
using detail::IsSnapshotHandler;

template <class T, typename ConfT>
struct Contract : public LifecycleContract<T, ConfT> {};

} // namespace Totem::TaskController
