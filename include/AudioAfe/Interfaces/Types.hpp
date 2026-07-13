#pragma once

#include "Types/Error.hpp"
#include <cstddef>
#include <cstdint>
#include <span>

namespace Totem::AudioAfe {

enum class VadState : uint8_t { Silence, Speech };

struct ProcessedFrameView {
    std::span<const int16_t> samples{};
    VadState vad = VadState::Silence;
    bool wakeDetected = false;
    int wakeWordIndex = 0;
    int wakeNetModelIndex = 0;
    int wakeWordLengthSamples = 0;
    float inputVolumeDb = 0.0F;
    float ringBufferFreeFraction = 0.0F;
    uint32_t timestampMs = 0;
};

struct InputBinding {
    void *owner = nullptr;
    std::size_t (*readBytes)(void *owner, uint8_t *data,
                             std::size_t bytes) = nullptr;

    [[nodiscard]] constexpr bool valid() const {
        return owner != nullptr && readBytes != nullptr;
    }

    std::size_t read(uint8_t *data, std::size_t bytes) const {
        return readBytes(owner, data, bytes);
    }
};

struct FrameSinkBinding {
    void *owner = nullptr;
    ReturnCode (*consumeFrame)(void *owner,
                               const ProcessedFrameView &frame) = nullptr;
    ReturnCode (*pipelineStopped)(void *owner) = nullptr;

    [[nodiscard]] constexpr bool valid() const {
        return owner != nullptr && consumeFrame != nullptr;
    }

    ReturnCode consume(const ProcessedFrameView &frame) const {
        return consumeFrame(owner, frame);
    }

    ReturnCode stop() const {
        return pipelineStopped == nullptr ? ReturnCode::from(CoreError::Ok)
                                          : pipelineStopped(owner);
    }
};

} // namespace Totem::AudioAfe
