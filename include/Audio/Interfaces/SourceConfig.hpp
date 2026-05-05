#pragma once

#include "Audio/Interfaces/Types.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Totem::Audio {

inline constexpr std::size_t a2dpSourceBufferBytes = 3072;

enum class AudioSourceKind : uint8_t {
    I2S,
    WavFile,
    A2DP,
    BtstackA2DP,
};

constexpr bool isAudioSourceKind(AudioSourceKind kind) {
    switch (kind) {
    case AudioSourceKind::I2S:
    case AudioSourceKind::WavFile:
    case AudioSourceKind::A2DP:
    case AudioSourceKind::BtstackA2DP:
        return true;
    default:
        return false;
    }
}

enum class I2SDevicePreset : uint8_t {
    LegacySoundCard,
    SPH0645,
    Custom,
};

struct I2SSourceReadinessConfig {
    uint16_t probeBytes = 64;
    uint16_t probeIntervalMs = 250;
    uint16_t waitingLogIntervalMs = 5000;
    uint16_t readTimeoutMs = 1;
    uint8_t emptyReadsBeforeOffline = 4;

    [[nodiscard]] bool validate() const {
        return probeBytes > 0 && probeBytes <= i2sMaxProbeBytes &&
               probeIntervalMs > 0 && waitingLogIntervalMs > 0 &&
               readTimeoutMs <= 100 && emptyReadsBeforeOffline > 0;
    }
};

struct I2SLinkConfig {
    AudioInfo audio{};
    I2SHostClockRole hostClockRole =
        I2SHostClockRole::ConsumesExternalClock;
    I2SFormat format = I2SFormat::Lsb;
    I2SChannelSelect channel = I2SChannelSelect::Left;
    uint8_t port = 0;
    bool useApll = true;

    [[nodiscard]] bool validate() const {
        return audio.validate();
    }
};

constexpr I2SLinkConfig legacySoundCardI2SLinkConfig() {
    return I2SLinkConfig{
        .audio =
            {
                .sampleRate = 96000,
                .channels = 1,
                .bitsPerSample = 32,
            },
        .hostClockRole = I2SHostClockRole::ConsumesExternalClock,
        .format = I2SFormat::Lsb,
        .channel = I2SChannelSelect::Left,
        .port = 0,
        .useApll = true,
    };
}

constexpr I2SLinkConfig sph0645I2SLinkConfig() {
    // The SPH0645 can run at higher sample rates, but 32 kHz keeps the
    // useful FFT range while reducing latency and CPU load on the media node.
    return I2SLinkConfig{
        .audio =
            {
                .sampleRate = 32000,
                .channels = 1,
                .bitsPerSample = 32,
            },
        .hostClockRole = I2SHostClockRole::ProvidesClock,
        .format = I2SFormat::Philips,
        .channel = I2SChannelSelect::Left,
        .port = 0,
        .useApll = true,
    };
}

struct I2SSourceConfig {
    I2SDevicePreset device = I2SDevicePreset::LegacySoundCard;
    I2SLinkConfig customLink{};
    I2SSourceReadinessConfig readiness{};
    I2SPins pins;

    [[nodiscard]] constexpr I2SLinkConfig resolvedLink() const {
        switch (device) {
        case I2SDevicePreset::LegacySoundCard:
            return legacySoundCardI2SLinkConfig();
        case I2SDevicePreset::SPH0645:
            return sph0645I2SLinkConfig();
        case I2SDevicePreset::Custom:
            return customLink;
        default:
            return {};
        }
    }

    [[nodiscard]] bool validate() const {
        return resolvedLink().validate() && readiness.validate();
    }
};

struct WavSourceConfig {
    const char *path = "/test.wav";
    bool loop = true;
    uint16_t waitingLogIntervalMs = 5000;

    [[nodiscard]] bool validate() const {
        return path != nullptr && path[0] != '\0' &&
               waitingLogIntervalMs > 0;
    }
};

enum class A2DPReaderMode : uint8_t {
    Stream,
    Raw,
};

constexpr bool isA2DPReaderMode(A2DPReaderMode mode) {
    switch (mode) {
    case A2DPReaderMode::Stream:
    case A2DPReaderMode::Raw:
        return true;
    default:
        return false;
    }
}

struct A2DPSourceConfig {
    const char *deviceName = "Totem Media";
    A2DPReaderMode readerMode = A2DPReaderMode::Stream;
    AudioInfo audio{
        .sampleRate = 44100,
        .channels = 1,
        .bitsPerSample = 16,
    };
    uint16_t bufferStartThresholdBytes = 768;
    uint16_t waitingLogIntervalMs = 5000;
    uint8_t taskCore = 1;
    uint8_t taskPriority = 10;

    [[nodiscard]] bool validate() const {
        return deviceName != nullptr && deviceName[0] != '\0' &&
               isA2DPReaderMode(readerMode) && audio.validate() &&
               audio.bitsPerSample == 16 &&
               (audio.channels == 1 || audio.channels == 2) &&
               bufferStartThresholdBytes > 0 &&
               bufferStartThresholdBytes <= a2dpSourceBufferBytes &&
               waitingLogIntervalMs > 0 &&
               taskCore <= 1 && taskPriority > 0;
    }
};

struct BtstackA2DPSourceConfig {
    const char *deviceName = "Totem Media";
    AudioInfo audio{
        .sampleRate = 44100,
        .channels = 1,
        .bitsPerSample = 16,
    };
    uint16_t bufferStartThresholdBytes = 768;
    uint16_t waitingLogIntervalMs = 5000;
    uint8_t taskCore = 1;
    uint8_t taskPriority = 10;
    uint16_t taskStackSize = 6144;

    [[nodiscard]] bool validate() const {
        return deviceName != nullptr && deviceName[0] != '\0' &&
               audio.validate() && audio.bitsPerSample == 16 &&
               (audio.channels == 1 || audio.channels == 2) &&
               bufferStartThresholdBytes > 0 &&
               bufferStartThresholdBytes <= a2dpSourceBufferBytes &&
               waitingLogIntervalMs > 0 && taskCore <= 1 &&
               taskPriority > 0 && taskStackSize >= 4096;
    }
};

struct AudioSourceConfig {
    AudioSourceKind kind = AudioSourceKind::I2S;
    std::optional<I2SSourceConfig> i2s = I2SSourceConfig{};
    std::optional<WavSourceConfig> wav = std::nullopt;
    std::optional<A2DPSourceConfig> a2dp = std::nullopt;
    std::optional<BtstackA2DPSourceConfig> btstackA2DP = std::nullopt;

    [[nodiscard]] bool validate() const {
        if (!isAudioSourceKind(kind)) {
            return false;
        }
        switch (kind) {
        case AudioSourceKind::I2S:
            return i2s.has_value() && i2s->validate();
        case AudioSourceKind::WavFile:
            return wav.has_value() && wav->validate();
        case AudioSourceKind::A2DP:
            return a2dp.has_value() && a2dp->validate();
        case AudioSourceKind::BtstackA2DP:
            return btstackA2DP.has_value() && btstackA2DP->validate();
        default:
            return false;
        }
    }
};

} // namespace Totem::Audio
