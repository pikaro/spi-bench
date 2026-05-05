#pragma once

#include "Audio/Interfaces/AnalyzerConfig.hpp"
#include "Audio/Interfaces/DisplayConfig.hpp"
#include "Audio/Interfaces/SourceConfig.hpp"
#include "Audio/Interfaces/Types.hpp"
#include "Data/Peripherals.hpp"
#include "LedPwm/Interfaces/Config.hpp"
#include "Platform/Hardware.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include "Wire/I2C/Interfaces/DisplayConfig.hpp"
#include "Wire/I2C/Interfaces/MasterConfig.hpp"
#include "Wire/I2C/Interfaces/Types.hpp"
#include "Wire/Spi/Interfaces/SlaveConfig.hpp"
#include "Wire/Spi/Interfaces/Types.hpp"

inline constexpr bool enableFftDebugDisplay = true;

#ifndef TOTEM_AUDIO_USE_BTSTACK_A2DP
#define TOTEM_AUDIO_USE_BTSTACK_A2DP 0
#endif

inline Totem::LedPwm::Config ledPwmConfig{
    .task =
        {
            .name = "BeatLed",
            .priority = 1,
            .core = Totem::TaskController::Config::CorePreference::specific(1),
            .stackSize = 4096,
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
            .stackSize = 3072,
            .intervalMs = 50,
            .noCatchup = true,
        },
};

inline Totem::Audio::AudioSourceConfig audioSourceConfig{
#if TOTEM_AUDIO_USE_BTSTACK_A2DP
    .kind = Totem::Audio::AudioSourceKind::BtstackA2DP,
#else
    .kind = Totem::Audio::AudioSourceKind::A2DP,
#endif
    .i2s =
        Totem::Audio::I2SSourceConfig{
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
        },
    .wav =
        Totem::Audio::WavSourceConfig{
            .path = "/test.wav",
            .loop = true,
        },
    .a2dp =
        Totem::Audio::A2DPSourceConfig{
            .deviceName = "Totem Media",
        },
    .btstackA2DP =
        Totem::Audio::BtstackA2DPSourceConfig{
            .deviceName = "Totem Media",
            .bufferStartThresholdBytes = 256,
            .taskPriority = 1,
            .cooperativeYieldIntervalMs = 4,
        },
};

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
            .stackSize = 3072,
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
            .priority = 2,
            .core = Totem::TaskController::Config::CorePreference::specific(0),
            .stackSize = 8192,
            .intervalMs = 10,
            .noCatchup = true,
            .useNotify = true,
            .notifyExpectTimeout = false,
            .notifyTimeoutMs = 10,
        },
    .attentionPin = Pin::GPIO17,
};
