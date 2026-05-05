#pragma once

#include "Audio/Interfaces/Types.hpp"
#include "Audio/detail/PlatformSelect.hpp"
#include <cstddef>
#include <cstdint>

namespace Totem::Audio::detail {

struct IAudioSource {
    virtual ~IAudioSource() = default;

    [[nodiscard]] virtual bool active() const = 0;
    [[nodiscard]] virtual const AudioInfo &audioInfo() const = 0;
    [[nodiscard]] virtual bool ready() const = 0;
    [[nodiscard]] virtual const char *sourceName() const = 0;
    [[nodiscard]] virtual uint32_t readinessProbeCount() const { return 0; }

    virtual bool pollReadiness(uint32_t nowMs) = 0;
    virtual void observeReadResult(std::size_t bytesRead, uint32_t nowMs) = 0;
    virtual Platform::AudioStream &stream() = 0;
};

} // namespace Totem::Audio::detail
