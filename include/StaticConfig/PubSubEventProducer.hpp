#pragma once

#include <cstddef>

struct PubSubEventProducerConfig {
    static constexpr size_t eventQueueSize = 8;
    static constexpr size_t publishPoolSize = 8;

    // ISR callbacks may bind only compact event identity/state. The complete
    // PubSub payload is constructed later by the producer task.
    static constexpr size_t maxFactorySize = 4;
    static constexpr size_t maxArgumentSize = 8;
};
