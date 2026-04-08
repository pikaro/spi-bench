#pragma once

namespace Totem::TaskController::detail::platform {

template <typename T> struct PlatformResultCreateTaskT {
    bool ok;
    T handle;
};

} // namespace Totem::TaskController::detail::platform
