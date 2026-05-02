#pragma once

#include "Audio/Interfaces/Types.hpp"
#include <cstdint>

namespace Totem::Audio {

enum class I2SDevicePreset : uint8_t {
    LegacySoundCard,
    SPH0645,
    Custom,
};

struct I2SSourceReadinessConfig {
    uint16_t probeBytes = 64;
    uint16_t probeIntervalMs = 250;
    uint16_t readTimeoutMs = 1;
    uint8_t emptyReadsBeforeOffline = 4;

    [[nodiscard]] bool validate() const {
        return probeBytes > 0 && probeBytes <= i2sMaxProbeBytes &&
               probeIntervalMs > 0 && readTimeoutMs <= 100 &&
               emptyReadsBeforeOffline > 0;
    }
};

struct I2SDeviceConfig {
    AudioInfo audio{};
    I2SRole role = I2SRole::Slave;
    I2SFormat format = I2SFormat::Lsb;
    I2SChannelSelect channel = I2SChannelSelect::Left;
    uint8_t port = 0;
    bool useApll = true;

    [[nodiscard]] bool validate() const {
        return audio.validate();
    }
};

constexpr I2SDeviceConfig legacySoundCardI2SDeviceConfig() {
    return I2SDeviceConfig{
        .audio =
            {
                .sampleRate = 96000,
                .channels = 1,
                .bitsPerSample = 32,
            },
        .role = I2SRole::Slave,
        .format = I2SFormat::Lsb,
        .channel = I2SChannelSelect::Left,
        .port = 0,
        .useApll = true,
    };
}

constexpr I2SDeviceConfig sph0645I2SDeviceConfig() {
    return I2SDeviceConfig{
        .audio =
            {
                .sampleRate = 64000,
                .channels = 1,
                .bitsPerSample = 32,
            },
        .role = I2SRole::Slave,
        .format = I2SFormat::Msb,
        .channel = I2SChannelSelect::Left,
        .port = 0,
        .useApll = true,
    };
}

struct I2SSourceConfig {
    I2SDevicePreset device = I2SDevicePreset::LegacySoundCard;
    I2SDeviceConfig customDevice{};
    I2SSourceReadinessConfig readiness{};
    I2SPins pins;

    [[nodiscard]] constexpr I2SDeviceConfig resolvedDevice() const {
        switch (device) {
        case I2SDevicePreset::LegacySoundCard:
            return legacySoundCardI2SDeviceConfig();
        case I2SDevicePreset::SPH0645:
            return sph0645I2SDeviceConfig();
        case I2SDevicePreset::Custom:
            return customDevice;
        default:
            return {};
        }
    }

    [[nodiscard]] bool validate() const {
        return resolvedDevice().validate() && readiness.validate();
    }
};

} // namespace Totem::Audio
