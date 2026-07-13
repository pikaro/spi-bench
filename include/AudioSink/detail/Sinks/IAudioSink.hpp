#pragma once

#include "AudioSink/Interfaces/Types.hpp"
#include "AudioSink/detail/PlatformSelect.hpp"

namespace Totem::AudioSink::detail {

struct IAudioSink {
    virtual ~IAudioSink() = default;

    [[nodiscard]] virtual bool active() const = 0;
    [[nodiscard]] virtual const AudioInfo &audioInfo() const = 0;
    [[nodiscard]] virtual bool ready() const = 0;
    [[nodiscard]] virtual const char *sinkName() const = 0;

    virtual Platform::AudioStream &stream() = 0;
};

} // namespace Totem::AudioSink::detail
