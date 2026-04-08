#pragma once

#include "TaskController/Interfaces/TaskRuntimeSnapshot.hh"
#include "Types/Error.hh"

namespace Totem::TaskController::detail {

template <typename Fn>
concept IsSnapshotHandler =
    std::is_invocable_r_v<ReturnCode, Fn, TaskRuntimeSnapshot>;

}
