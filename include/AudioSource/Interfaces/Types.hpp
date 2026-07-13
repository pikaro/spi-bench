#pragma once

#include "Platform/Hardware.hpp"
#include <cstddef>
#include <cstdint>

namespace Totem::AudioSource {

inline constexpr size_t i2sMaxProbeBytes = 256;

enum class I2SHostClockRole : uint8_t {
    ConsumesExternalClock,
    ProvidesClock,
};

enum class I2SFormat : uint8_t {
    Standard,
    Lsb,
    Msb,
    Philips,
    RightJustified,
    LeftJustified,
    Pcm,
};

enum class I2SChannelSelect : uint8_t {
    Stereo,
    Left,
    Right,
};

struct AudioInfo {
    uint32_t sampleRate = 96000;
    uint16_t channels = 1;
    uint8_t bitsPerSample = 32;

    [[nodiscard]] bool validate() const {
        return sampleRate > 0 && sampleRate <= 384000 && channels > 0 &&
               channels <= 8 &&
               (bitsPerSample == 8 || bitsPerSample == 16 ||
                bitsPerSample == 24 || bitsPerSample == 32);
    }

    [[nodiscard]] uint16_t bytesPerFrame() const {
        return static_cast<uint16_t>((bitsPerSample / 8U) * channels);
    }
};

struct I2SPins {
    Pin bitClock;
    Pin wordSelect;
    Pin dataIn;
};

struct I2SSourceStatus {
    bool ready = false;
    uint32_t probes = 0;
    uint32_t emptyReads = 0;
    uint32_t readTimeouts = 0;
    uint32_t readErrors = 0;
    uint32_t observedBytes = 0;
    uint32_t lastDataMs = 0;
    int32_t lastReadStatus = 0;
};

} // namespace Totem::AudioSource
