#pragma once

#include <cstdint>
namespace Totem::RingBuffer::detail {

struct PlatformConfigAbstraction {
    enum class Type : uint8_t {
        Message = 0,
        ByteStream,
    };
    bool requiresContiguousStorage = true;

    Type type = Type::Message;
};

} // namespace Totem::RingBuffer::detail
