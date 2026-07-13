#pragma once

#include "AudioSource/Interfaces/Types.hpp"
#include "Platform/Hardware.hpp"
#include <cstddef>
#include <cstdint>

namespace Totem::AudioSink {

using AudioInfo = AudioSource::AudioInfo;
using I2SChannelSelect = AudioSource::I2SChannelSelect;
using I2SFormat = AudioSource::I2SFormat;
using I2SHostClockRole = AudioSource::I2SHostClockRole;

inline constexpr std::size_t webSocketDefaultPacketBytes = 1024;
inline constexpr std::size_t webSocketMaxPacketBytes = 4096;
inline constexpr std::size_t webSocketMaxBearerTokenBytes = 256;

struct I2SOutputPins {
    Pin bitClock;
    Pin wordSelect;
    Pin dataOut;
};

struct AudioSinkStatus {
    bool ready = false;
    uint32_t reconnects = 0;
    uint32_t writeFailures = 0;
    uint32_t writtenBytes = 0;
};

} // namespace Totem::AudioSink
