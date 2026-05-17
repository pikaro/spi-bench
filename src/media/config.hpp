#pragma once

#include "Audio/Interfaces/AnalyzerConfig.hpp"
#include "Audio/Interfaces/DisplayConfig.hpp"
#include "Audio/Interfaces/SourceConfig.hpp"
#include "Audio/Interfaces/Types.hpp"
#include "Audio/detail/Sources/I2SSource.hpp"
#include "Audio/detail/Sources/WavSource.hpp"
#include "Data/Peripherals.hpp"
#include "LedPwm/Interfaces/Config.hpp"
#include "Platform/Hardware.hpp"
#include "StaticConfig/Stacks.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include "Wire/I2C/Interfaces/DisplayConfig.hpp"
#include "Wire/I2C/Interfaces/MasterConfig.hpp"
#include "Wire/I2C/Interfaces/Types.hpp"
#include "Wire/Spi/Interfaces/SlaveConfig.hpp"
#include "Wire/Spi/Interfaces/Types.hpp"
#include "Types/Error.hpp"

namespace Totem::Audio::detail {
class A2DPSource;
class BtstackA2DPSource;
} // namespace Totem::Audio::detail

inline constexpr bool enableFftDebugDisplay = true;

inline Totem::LedPwm::Config ledPwmConfig{
    .task =
        {
            .name = "BeatLed",
            .priority = 1,
            .core = Totem::TaskController::Config::CorePreference::specific(1),
            .stackSize = Totem::StaticConfig::TaskStacks::ledPwm,
            .intervalMs = 5,
            .noCatchup = true,
        },
    .leds = {{
        {
            .led = PeripheralLed::BeatIndicator,
            .pin = Pin::GPIO26,
            .configured = true,
        },
    }},
};

inline Totem::Wire::I2C::MasterConfig i2cMasterConfig{
    .busId = Totem::Wire::I2C::BusId::Bus0,
    .pins =
        {
            .sda = Pin::GPIO22,
            .scl = Pin::GPIO21,
        },
    .clockHz = 1'000'000,
    .enableInternalPullups = true,
    .transactionTimeoutMs = 50,
};

inline Totem::Wire::I2C::Ssd1306Config fftDisplayConfig{
    .device =
        {
            .address = 0x3C,
            .clockHz = 1'000'000,
        },
    .width = 128,
    .height = 32,
};

inline Totem::Audio::FftDisplayConfig fftDisplayVisualizerConfig{
    .showRawBands = false,
    .beatBarHoldMs = 90,
    .task =
        {
            .name = "FftDisplay",
            .priority = 1,
            .core = Totem::TaskController::Config::CorePreference::specific(1),
            .stackSize = Totem::StaticConfig::TaskStacks::audioFftDisplay,
            .intervalMs = 50,
            .noCatchup = true,
        },
};

inline constexpr Totem::Audio::AudioSourceKind mediaAudioSourceKind =
    Totem::Audio::AudioSourceKind::I2S;

inline constexpr Totem::Audio::I2SSourceConfig i2sAudioSourceConfig{
    .device = Totem::Audio::I2SDevicePreset::SPH0645,
    .readiness =
        {
            .probeBytes = 64,
            .probeIntervalMs = 250,
            .waitingLogIntervalMs = 5000,
            .readTimeoutMs = 2,
            .emptyReadsBeforeOffline = 64,
        },
    .pins =
        {
            .bitClock = Pin::GPIO25,
            .wordSelect = Pin::GPIO32,
            .dataIn = Pin::GPIO33,
        },
};

inline constexpr Totem::Audio::WavSourceConfig wavAudioSourceConfig{
    .path = "/test.wav",
    .loop = true,
};

inline constexpr Totem::Audio::A2DPSourceConfig a2dpAudioSourceConfig{
    .deviceName = "Totem Media",
};

inline constexpr Totem::Audio::BtstackA2DPSourceConfig
    btstackA2DPAudioSourceConfig{
        .deviceName = "Totem Media",
        .bufferStartThresholdBytes = 256,
        .taskPriority = 1,
        .cooperativeYieldIntervalMs = 4,
};

template <Totem::Audio::AudioSourceKind Kind>
struct MediaAudioSourceBinding;

template <>
struct MediaAudioSourceBinding<Totem::Audio::AudioSourceKind::I2S> {
    using Source = Totem::Audio::detail::I2SSource;
};

template <>
struct MediaAudioSourceBinding<Totem::Audio::AudioSourceKind::WavFile> {
    using Source = Totem::Audio::detail::WavSource;
};

template <>
struct MediaAudioSourceBinding<Totem::Audio::AudioSourceKind::A2DP> {
    using Source = Totem::Audio::detail::A2DPSource;
};

template <>
struct MediaAudioSourceBinding<Totem::Audio::AudioSourceKind::BtstackA2DP> {
    using Source = Totem::Audio::detail::BtstackA2DPSource;
};

using MediaAudioSourceType =
    typename MediaAudioSourceBinding<mediaAudioSourceKind>::Source;

template <Totem::Audio::AudioSourceKind Kind>
inline ReturnCode beginMediaAudioSource(
    typename MediaAudioSourceBinding<Kind>::Source &source) {
    if constexpr (Kind == Totem::Audio::AudioSourceKind::I2S) {
        return source.begin(i2sAudioSourceConfig);
    } else if constexpr (Kind == Totem::Audio::AudioSourceKind::WavFile) {
        return source.begin(wavAudioSourceConfig);
    } else if constexpr (Kind == Totem::Audio::AudioSourceKind::A2DP) {
        return source.begin(a2dpAudioSourceConfig);
    } else if constexpr (Kind ==
                         Totem::Audio::AudioSourceKind::BtstackA2DP) {
        return source.begin(btstackA2DPAudioSourceConfig);
    } else {
        static_assert(Totem::Audio::isAudioSourceKind(Kind),
                      "Unsupported media audio source kind");
    }
}

inline ReturnCode beginMediaAudioSource(MediaAudioSourceType &source) {
    return beginMediaAudioSource<mediaAudioSourceKind>(source);
}

inline Totem::Audio::FftAnalyzerConfig fftAnalyzerConfig{
    .backend = Totem::Audio::FftBackendLibrary::EspressifFft,
    .length = 1024,
    .stride = 1024,
    .copyBufferSizeBytes = 512,
    .task =
        {
            .name = "AudioFft",
            .priority = 2,
            .core = Totem::TaskController::Config::CorePreference::specific(1),
            .stackSize = Totem::StaticConfig::TaskStacks::audioFft,
            .intervalMs = 4,
            .noCatchup = true,
        },
};

inline Totem::Wire::Spi::SlaveConfig spiSlaveConfig{
    .busId = Totem::Wire::Spi::BusId::Bus3,
    .pins =
        {
            .mosiPin = Pin::VSPI_MOSI,
            .misoPin = Pin::VSPI_MISO,
            .sclkPin = Pin::VSPI_SCLK,
        },
    .csPin = Pin::VSPI_CS,
    .maxTransferSize = 4096,
    .transferWindowBytes = 256,
    .maxOutboundSlotBytes = 256,
    .mode = Totem::Wire::Spi::Mode::Mode0,
    .queueSize = 1,
    .task =
        {
            .name = "SpiSlaveTask",
            .priority = 4,
            .core = Totem::TaskController::Config::CorePreference::specific(0),
            .stackSize = Totem::StaticConfig::TaskStacks::spiSlave,
            .intervalMs = 10,
            .noCatchup = true,
            .useNotify = true,
            .notifyExpectTimeout = false,
            .notifyTimeoutMs = 10,
        },
    .attentionPin = Pin::GPIO17,
};

inline constexpr Totem::StatusLed::Config statusLedConfig{
    .configured = true,
    .pin = Pin::GPIO16,
};
