#pragma once

#include "Platform/Hardware.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Totem::Audio {

inline constexpr size_t fftBandCount = 8;
inline constexpr size_t fftMaxFrameHandlers = 4;
inline constexpr size_t fftMaxBeatHandlers = 4;
inline constexpr size_t i2sMaxProbeBytes = 256;

enum class I2SRole : uint8_t {
    Slave,
    Master,
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
    uint32_t observedBytes = 0;
    uint32_t lastDataMs = 0;
};

enum class FftBand : uint8_t {
    SubBass,
    Bass,
    LowMid,
    Mid,
    HighMid,
    Presence,
    Brilliance,
    Air,
};

struct FftBandRange {
    uint16_t lowerHz = 0;
    uint16_t upperHz = 0;

    [[nodiscard]] bool validate() const {
        return lowerHz > 0 && upperHz > lowerHz;
    }
};

using FftBandRanges = std::array<FftBandRange, fftBandCount>;

constexpr FftBandRanges defaultFftBandRanges() {
    return FftBandRanges{{
        {.lowerHz = 20, .upperHz = 60},
        {.lowerHz = 60, .upperHz = 150},
        {.lowerHz = 150, .upperHz = 400},
        {.lowerHz = 400, .upperHz = 1000},
        {.lowerHz = 1000, .upperHz = 2500},
        {.lowerHz = 2500, .upperHz = 5000},
        {.lowerHz = 5000, .upperHz = 10000},
        {.lowerHz = 10000, .upperHz = 20000},
    }};
}

constexpr std::array<float, fftBandCount> flatFftBandGains() {
    return std::array<float, fftBandCount>{{
        1.0F,
        1.0F,
        1.0F,
        1.0F,
        1.0F,
        1.0F,
        1.0F,
        1.0F,
    }};
}

enum class FftWindow : uint8_t {
    None,
    Hamming,
    Hann,
};

enum class FftMagnitudeMode : uint8_t {
    Average,
    Sum,
    Rms,
};

enum class FftWeighting : uint8_t {
    None,
    AWeighting,
};

struct FftMagnitudeCacheConfig {
    float floorAdaptAlpha = 0.005F;
    float peakAdaptAlpha = 0.02F;
    float minimumRange = 1.0F;

    [[nodiscard]] bool validate() const {
        return floorAdaptAlpha >= 0.0F && floorAdaptAlpha <= 1.0F &&
               peakAdaptAlpha >= 0.0F && peakAdaptAlpha <= 1.0F &&
               minimumRange > 0.0F;
    }
};

struct FftBandIndexRange {
    uint8_t lower = 0;
    uint8_t upper = 1;

    [[nodiscard]] bool validate() const {
        return lower <= upper && upper < fftBandCount;
    }
};

enum class BeatEventKind : uint8_t {
    Detected,
};

struct FftBandValue {
    FftBand band = FftBand::SubBass;
    uint16_t lowerHz = 0;
    uint16_t upperHz = 0;
    uint16_t lowerBin = 0;
    uint16_t upperBin = 0;
    float magnitude = 0.0F;
    float weightedMagnitude = 0.0F;
    float floor = 0.0F;
    float peak = 0.0F;
    uint8_t scaled = 0;
};

struct FftFrame {
    AudioInfo audio{};
    uint32_t sequence = 0;
    uint64_t timestampUs = 0;
    uint16_t length = 0;
    uint16_t stride = 0;
    std::array<FftBandValue, fftBandCount> bands{};
};

struct BeatEvent {
    BeatEventKind kind = BeatEventKind::Detected;
    uint32_t frameSequence = 0;
    uint64_t timestampUs = 0;
    uint8_t energy = 0;
    float bpm = 0.0F;
};

struct FftRuntimeStats {
    uint32_t copiedBytes = 0;
    uint32_t emptyCopies = 0;
    uint32_t sourceUnavailableSkips = 0;
    uint32_t frames = 0;
    uint32_t droppedFrames = 0;
    uint32_t beats = 0;
    uint32_t maxCopyUs = 0;
    uint32_t maxFrameUs = 0;
};

using FftFrameCallback = ReturnCode (*)(void *owner, const FftFrame &frame);
using BeatCallback = ReturnCode (*)(void *owner, const BeatEvent &event);

struct FftFrameHandler {
    void *owner = nullptr;
    FftFrameCallback callback = nullptr;

    [[nodiscard]] bool valid() const { return callback != nullptr; }
};

struct BeatHandler {
    void *owner = nullptr;
    BeatCallback callback = nullptr;

    [[nodiscard]] bool valid() const { return callback != nullptr; }
};

} // namespace Totem::Audio
