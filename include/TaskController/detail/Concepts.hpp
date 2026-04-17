#pragma once

#include "TaskController/Interfaces/TaskRuntimeSnapshot.hpp"
#include "Types/Error.hpp"

namespace Totem::TaskController::detail {

template <typename Fn>
concept IsSnapshotHandler =
    std::is_invocable_r_v<ReturnCode, Fn, TaskRuntimeSnapshot>;

}
