#pragma once

#include "AudioFft/Interfaces/AnalyzerConfig.hpp"
#include "AudioFft/Interfaces/DisplayConfig.hpp"
#include "AudioFft/Interfaces/Types.hpp"
#include "AudioSource/Interfaces/SourceConfig.hpp"
#include "AudioSource/detail/Sources/I2SSource.hpp"
#include "AudioSource/detail/Sources/WavSource.hpp"
#include "Data/Peripherals.hpp"
#include "LedPwm/Interfaces/Config.hpp"
#include "Platform/Hardware.hpp"
#include "StaticConfig/Stacks.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include "Types/Error.hpp"
#include "Wire/I2C/Interfaces/DisplayConfig.hpp"
#include "Wire/I2C/Interfaces/MasterConfig.hpp"
#include "Wire/I2C/Interfaces/Types.hpp"
#include "Wire/Spi/Interfaces/SlaveConfig.hpp"
#include "Wire/Spi/Interfaces/Types.hpp"

namespace Totem::AudioSource::detail {
class A2DPSource;
class BtstackA2DPSource;
} // namespace Totem::AudioSource::detail

inline constexpr bool enableFftDebugDisplay = true;

inline Totem::LedPwm::Config ledPwmConfig{
    .task =
        {
            .name = "PeakLed",
            .priority = 1,
            .core = Totem::TaskController::Config::CorePreference::specific(1),
            .stackSize = Totem::StaticConfig::TaskStacks::ledPwm,
            .intervalMs = 5,
            .noCatchup = true,
        },
    .platform =
        {
            .outputInverted = true,
        },
    .leds = {{
        {
            .led = PeripheralLed::PeakIndicator,
            .pin = Pin::RX,
            .configured = true,
        },
    }},
};

inline Totem::Wire::I2C::MasterConfig i2cMasterConfig{
    .busId = Totem::Wire::I2C::BusId::Bus0,
    .pins =
        {
            .sda = Pin::GPIO6,
            .scl = Pin::GPIO5,
        },
    // .clockHz = 1'000'000,
    .clockHz = 400'000,
    .enableInternalPullups = false,
    .transactionTimeoutMs = 50,
};

inline Totem::Wire::I2C::Ssd1306Config fftDisplayConfig{
    .device =
        {
            .address = 0x3C,
            // .clockHz = 1'000'000,
            .clockHz = 400'000,
        },
    .width = 128,
    .height = 32,
};

inline Totem::AudioFft::FftDisplayConfig fftDisplayVisualizerConfig{
    .showRawBands = false,
    .peakBarHoldMs = 90,
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

inline constexpr Totem::AudioSource::AudioSourceKind mediaAudioSourceKind =
    Totem::AudioSource::AudioSourceKind::I2S;

inline constexpr Totem::AudioSource::I2SSourceConfig i2sAudioSourceConfig{
    .device = Totem::AudioSource::I2SDevicePreset::SPH0645,
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
            .bitClock = Pin::GPIO13,
            .wordSelect = Pin::GPIO1,
            .dataIn = Pin::GPIO12,
        },
};

inline constexpr Totem::AudioSource::WavSourceConfig wavAudioSourceConfig{
    .path = "/test.wav",
    .loop = true,
};

inline constexpr Totem::AudioSource::A2DPSourceConfig a2dpAudioSourceConfig{
    .deviceName = "Totem Media",
};

inline constexpr Totem::AudioSource::BtstackA2DPSourceConfig
    btstackA2DPAudioSourceConfig{
        .deviceName = "Totem Media",
        .bufferStartThresholdBytes = 256,
        .taskPriority = 1,
        .cooperativeYieldIntervalMs = 4,
    };

template <Totem::AudioSource::AudioSourceKind Kind>
struct MediaAudioSourceBinding;

template <>
struct MediaAudioSourceBinding<Totem::AudioSource::AudioSourceKind::I2S> {
    using Source = Totem::AudioSource::detail::I2SSource;
};

template <>
struct MediaAudioSourceBinding<Totem::AudioSource::AudioSourceKind::WavFile> {
    using Source = Totem::AudioSource::detail::WavSource;
};

template <>
struct MediaAudioSourceBinding<Totem::AudioSource::AudioSourceKind::A2DP> {
    using Source = Totem::AudioSource::detail::A2DPSource;
};

template <>
struct MediaAudioSourceBinding<
    Totem::AudioSource::AudioSourceKind::BtstackA2DP> {
    using Source = Totem::AudioSource::detail::BtstackA2DPSource;
};

using MediaAudioSourceType =
    typename MediaAudioSourceBinding<mediaAudioSourceKind>::Source;

template <Totem::AudioSource::AudioSourceKind Kind>
inline ReturnCode
beginMediaAudioSource(typename MediaAudioSourceBinding<Kind>::Source &source) {
    if constexpr (Kind == Totem::AudioSource::AudioSourceKind::I2S) {
        return source.begin(i2sAudioSourceConfig);
    } else if constexpr (Kind == Totem::AudioSource::AudioSourceKind::WavFile) {
        return source.begin(wavAudioSourceConfig);
    } else if constexpr (Kind == Totem::AudioSource::AudioSourceKind::A2DP) {
        return source.begin(a2dpAudioSourceConfig);
    } else if constexpr (Kind ==
                         Totem::AudioSource::AudioSourceKind::BtstackA2DP) {
        return source.begin(btstackA2DPAudioSourceConfig);
    } else {
        static_assert(Totem::AudioSource::isAudioSourceKind(Kind),
                      "Unsupported media audio source kind");
    }
}

inline ReturnCode beginMediaAudioSource(MediaAudioSourceType &source) {
    return beginMediaAudioSource<mediaAudioSourceKind>(source);
}

inline Totem::AudioFft::FftAnalyzerConfig fftAnalyzerConfig{
    .backend = Totem::AudioFft::FftBackendLibrary::EspressifFft,
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
            .mosiPin = Pin::GPIO9,
            .misoPin = Pin::GPIO11,
            .sclkPin = Pin::GPIO10,
        },
    .csPin = Pin::GPIO8,
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
    .attentionPin = Pin::GPIO7,
};

inline constexpr Totem::StatusLed::Config statusLedConfig{
    .configured = true,
    .pin = Pin::StatusLed,
};
