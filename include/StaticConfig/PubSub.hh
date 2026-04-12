#pragma once

#include <cstddef>
struct PubSubConfig {
    static constexpr size_t maxTopics = 32;
    static constexpr size_t maxTopicNameLength = 16;
    static constexpr size_t maxSubscribers = 8;
    static constexpr size_t maxSubscriberNameLength = 8;
    static constexpr size_t maxTransports = 8;
    static constexpr size_t maxTransportNameLength = 16;

    static constexpr size_t maxPayloadSize = 32;
    static constexpr size_t maxMessageQueueSize = 16;
    static constexpr size_t maxInFlightMessages = 16;

    static constexpr size_t ingressBufferSize = 512;
    static constexpr size_t maxIngressSpans = 64;
    static constexpr size_t maxIngressRecords = 64;
    static constexpr size_t maxIngressRecordAgeMs = 1000;

    static constexpr size_t defaultEgressBufferSize = 512;
    static constexpr size_t maxEgressSpans = 64;
    static constexpr size_t maxEgressRecords = 64;
    static constexpr size_t maxEgressRecordAgeMs = 1000;

    [[nodiscard]] static bool validate() { return true; }
};
