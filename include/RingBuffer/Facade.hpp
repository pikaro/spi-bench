#pragma once

#include "RingBuffer/detail/Config.hpp"
#include "RingBuffer/detail/PlatformSelect.hpp"

namespace Totem::RingBuffer {

using Buffer = detail::RingBuffer;
using Handle = detail::RingBufferHandle;
using Config = detail::PlatformConfigAbstraction;

template <class T> struct Contract {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Ring buffer data must be trivially copyable");
};

} // namespace Totem::RingBuffer
