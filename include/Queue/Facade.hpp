#pragma once

#include "Queue/detail/PlatformSelect.hpp"
#include <type_traits>

namespace Totem::Queue {

using Platform = detail::Platform;
using Handle = detail::QueueHandle;

template <class T> struct Contract {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Queue data must be trivially copyable");
};

} // namespace Totem::Queue
