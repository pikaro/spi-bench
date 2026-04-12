#pragma once

#include <cstddef>
struct PubSubConfig {
    static constexpr size_t maxTopics = 32;
    static constexpr size_t maxTopicNameLength = 16;
    static constexpr size_t maxPayloadSize = 32;
    static constexpr size_t maxSubscribers = 8;
    static constexpr size_t maxSubscriberNameLength = 8;
    static constexpr size_t maxTransports = 8;
    static constexpr size_t maxTransportNameLength = 16;
    static constexpr size_t maxMessageQueueSize = 16;
    static constexpr size_t maxInFlightMessages = 16;

    [[nodiscard]] static bool validate() { return true; }
};
