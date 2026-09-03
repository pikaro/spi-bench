#pragma once

#include <cstdint>
namespace Totem::Data {

enum class NodeName : uint8_t {
    Master,
    Media,
    InputOutput,
    GPUNode0,
    GPUNode1,
    GPUNode2,
    GPUNode3,
    Power,
};

}
