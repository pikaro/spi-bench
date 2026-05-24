#pragma once

#include "Audio/Interfaces/Types.hpp"
#include "Audio/Interfaces/Wire.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "Queue/Facade.hpp"
#include "Services/PubSub.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace MediaAudioPubSub {

struct Config {
    bool publishFftFrames = true;
    bool publishBeatEvents = true;
};

inline constexpr Config config{};
inline constexpr size_t fftFrameQueueSize = 2;
inline constexpr size_t beatEventQueueSize = 8;
inline constexpr size_t fftPublishPoolSize = 4;
inline constexpr size_t beatPublishPoolSize = 8;

namespace detail {

inline Totem::Queue::Platform::Storage<Totem::Audio::FftFrame,
                                       fftFrameQueueSize>
    fftFrameQueueStorage{};
inline Totem::Queue::Platform::Storage<Totem::Audio::BeatEvent,
                                       beatEventQueueSize>
    beatEventQueueStorage{};
inline Totem::Queue::Handle fftFrameQueue = nullptr;
inline Totem::Queue::Handle beatEventQueue = nullptr;
inline Totem::PubSubBackend::Pool<Totem::Audio::FftFrame, fftPublishPoolSize>
    fftPool{PubSubService::nextMessageId};
inline Totem::PubSubBackend::Pool<Totem::Audio::BeatEvent, beatPublishPoolSize>
    beatPool{PubSubService::nextMessageId};
inline uint32_t droppedBackpressurePayloads = 0;

inline bool isBackpressure(ReturnCode ret) {
    return ret == ERR(Overflow) || ret == ERR(CoreError, Overflow);
}

inline void noteBackpressureDrop(NodeData::PubSub::Topic topic,
                                 ReturnCode reason) {
    const auto dropped = ++droppedBackpressurePayloads;
    if (dropped == 1 || (dropped % 64U) == 0) {
        _log_w("Dropping noncritical media PubSub payload for topic 0x%08lx "
               "after backpressure: " ERR_FMT " (dropped=%lu)",
               static_cast<unsigned long>(topic), ERR_ARG(reason),
               static_cast<unsigned long>(dropped));
    }
}

inline uint8_t clampU8(float value) {
    if (!std::isfinite(value) || value <= 0.0F) {
        return 0;
    }
    return static_cast<uint8_t>(
        std::min<long>(std::lround(value), 255L));
}

inline Totem::Audio::FftFrame makeWireFrame(
    const Totem::Audio::FftResult &frame) {
    return Totem::Audio::FftFrame{
        .subBass = frame.bands[0].scaled,
        .bass = frame.bands[1].scaled,
        .lowMid = frame.bands[2].scaled,
        .mid = frame.bands[3].scaled,
        .highMid = frame.bands[4].scaled,
        .presence = frame.bands[5].scaled,
        .brilliance = frame.bands[6].scaled,
        .air = frame.bands[7].scaled,
    };
}

inline Totem::Audio::BeatEvent makeWireBeat(
    const Totem::Audio::BeatResult &event) {
    return Totem::Audio::BeatEvent{
        .group = event.group,
        .bpm = clampU8(event.bpm),
        .energy = event.energy,
        .tension = 0,
    };
}

template <typename T, size_t PoolSize>
inline ReturnCode publish(T payload, NodeData::PubSub::Topic topic,
                          Totem::PubSubBackend::Pool<T, PoolSize> &pool) {
    FAIL_IF_NOT(PubSubService::configured(), ERR(CoreError, InvalidState),
                "PubSub backend is not configured for media audio publish");

    auto stored = pool.store(payload);
    if (!stored) {
        if (isBackpressure(stored.error())) {
            noteBackpressureDrop(topic, stored.error());
            return OK();
        }
        FAIL_ERR_FWD(stored.error(),
                     "Failed to store media audio PubSub payload");
    }

    auto envelopeResult = Totem::PubSubBackend::Envelope::make<T>({
        .owner = static_cast<void *>(&pool),
        .topic = topic,
        .messageId = *stored,
        .getPayloadPtr = Totem::PubSubBackend::Pool<T, PoolSize>::getPtr,
        .encodePayload = Totem::PubSubBackend::Pool<T, PoolSize>::encodePayload,
        .release = Totem::PubSubBackend::Pool<T, PoolSize>::release,
        .requireSyncedClock = false,
    });
    if (!envelopeResult) {
        (void)pool.release({.header = {.messageId = *stored}});
        FAIL_ERR_FWD(envelopeResult.error(),
                     "Failed to create media audio PubSub envelope");
    }

    auto publishResult = PubSubService::get().publish(*envelopeResult);
    if (!publishResult.ok()) {
        (void)pool.release(*envelopeResult);
        if (isBackpressure(publishResult)) {
            noteBackpressureDrop(topic, publishResult);
            return OK();
        }
        FAIL_ERR_FWD(publishResult,
                     "Failed to publish media audio PubSub envelope");
    }
    return OK();
}

template <typename T>
inline void enqueueLatest(Totem::Queue::Handle queue, const T &value) {
    auto ret = Totem::Queue::Platform::send(queue, &value, 0);
    if (ret.ok()) {
        return;
    }

    T dropped{};
    (void)Totem::Queue::Platform::receive(queue, &dropped, 0);
    (void)Totem::Queue::Platform::send(queue, &value, 0);
}

inline ReturnCode onFrame(void * /*unused*/,
                          const Totem::Audio::FftResult &frame) {
    if (!config.publishFftFrames || fftFrameQueue == nullptr) {
        return OK();
    }
    enqueueLatest(fftFrameQueue, makeWireFrame(frame));
    return OK();
}

inline ReturnCode onBeat(void * /*unused*/,
                         const Totem::Audio::BeatResult &event) {
    if (!config.publishBeatEvents || beatEventQueue == nullptr) {
        return OK();
    }
    const auto beat = makeWireBeat(event);
    (void)Totem::Queue::Platform::send(beatEventQueue, &beat, 0);
    return OK();
}

} // namespace detail

template <typename Analyzer>
inline ReturnCode begin(Analyzer &analyzer) {
    if (detail::fftFrameQueue == nullptr) {
        auto queueResult =
            Totem::Queue::Platform::create(detail::fftFrameQueueStorage);
        if (!queueResult) {
            FAIL_ERR_FWD(queueResult.error(),
                         "Failed to create media FFT PubSub queue");
        }
        detail::fftFrameQueue = *queueResult;
    }
    if (detail::beatEventQueue == nullptr) {
        auto queueResult =
            Totem::Queue::Platform::create(detail::beatEventQueueStorage);
        if (!queueResult) {
            FAIL_ERR_FWD(queueResult.error(),
                         "Failed to create media beat PubSub queue");
        }
        detail::beatEventQueue = *queueResult;
    }

    if (config.publishFftFrames) {
        FAIL_IF_ERR_FWD(analyzer.addFrameHandler({
                            .owner = nullptr,
                            .callback = detail::onFrame,
                        }),
                        "Failed to register media FFT PubSub handler");
    }
    if (config.publishBeatEvents) {
        FAIL_IF_ERR_FWD(analyzer.addBeatHandler({
                            .owner = nullptr,
                            .callback = detail::onBeat,
                        }),
                        "Failed to register media beat PubSub handler");
    }
    return OK();
}

inline ReturnCode work() {
    if (detail::beatEventQueue != nullptr) {
        Totem::Audio::BeatEvent beat{};
        while (
            Totem::Queue::Platform::receive(detail::beatEventQueue, &beat, 0)
                .ok()) {
            FAIL_IF_ERR_FWD(detail::publish(
                                beat, NodeData::PubSub::Topic::Beat,
                                detail::beatPool),
                            "Failed to publish queued beat event");
        }
    }

    if (detail::fftFrameQueue != nullptr) {
        Totem::Audio::FftFrame frame{};
        Totem::Audio::FftFrame latest{};
        bool hasFrame = false;
        while (
            Totem::Queue::Platform::receive(detail::fftFrameQueue, &frame, 0)
                .ok()) {
            latest = frame;
            hasFrame = true;
        }
        if (hasFrame) {
            FAIL_IF_ERR_FWD(detail::publish(
                                latest, NodeData::PubSub::Topic::FftFrame,
                                detail::fftPool),
                            "Failed to publish queued FFT frame");
        }
    }

    return OK();
}

} // namespace MediaAudioPubSub
