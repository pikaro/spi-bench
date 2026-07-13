#pragma once

#include "AudioAfe/Facade.hpp"
#include "AudioSink/Facade.hpp"
#include "AudioSource/Interfaces/SourceConfig.hpp"
#include "Platform/Hardware.hpp"
#include "StaticConfig/Wifi.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "Wifi/Interfaces/Config.hpp"
#include "Wifi/Interfaces/Types.hpp"
#include "audio_session.hpp"
#include "pcm16_downsampler.hpp"

inline constexpr bool enableFftDebugDisplay = true;

inline constexpr Totem::StatusLed::Config statusLedConfig{
    .configured = true,
    .backend = Totem::StatusLed::OutputBackend::SplitRgbGpio,
    .splitRgbGpio =
        {
            .red = Pin::LED_RED,
            .green = Pin::LED_GREEN,
            .blue = Pin::LED_BLUE,
            .activeHigh = false,
        },
};

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
            .bitClock = Pin::A1,
            .wordSelect = Pin::A0,
            .dataIn = Pin::A2,
        },
};

inline constexpr AiAudio::Pcm16DownsamplerConfig nemoAsrDownsamplerConfig =
    AiAudio::defaultNemoAsrDownsamplerConfig;

inline constexpr Totem::AudioAfe::Config audioAfeConfig{
    .modelPartition = "model",
    .performance = Totem::AudioAfe::PerformanceMode::HighPerformance,
    .memory = Totem::AudioAfe::MemoryAllocation::PreferPsram,
    .noiseSuppression =
        {
            .enabled = true,
            .mode = Totem::AudioAfe::NoiseSuppressionMode::WebRtc,
            .modelName = nullptr,
        },
    .vad =
        {
            .enabled = true,
            .implementation = Totem::AudioAfe::VadImplementation::Neural,
            .mode = Totem::AudioAfe::VadMode::Normal,
            .modelName = nullptr,
            .minimumSpeechMs = 128,
            .minimumSilenceMs = 800,
            .lookbackMs = 128,
            .mutePlayback = false,
            .enableChannelTrigger = false,
        },
    .wakeNet =
        {
            .enabled = true,
            .modelName = nullptr,
            .mode = Totem::AudioAfe::WakeNetMode::Normal,
            .threshold = 0.0F,
            .modelIndex = 1,
        },
    .agc =
        {
            .enabled = true,
            .mode = Totem::AudioAfe::AgcMode::WakeNet,
            .compressionGainDb = 9,
            .targetLevelDbfs = 3,
            .linearGain = 1.0F,
        },
    .acousticEchoCancellation = false,
    .speechEnhancement = false,
    .afeCore = 1,
    .afePriority = 5,
    .afeRingBufferFrames = 50,
    .maximumFeedSamples = Totem::StaticConfig::AudioAfe::maxFeedSamples,
    .maximumFetchSamples = Totem::StaticConfig::AudioAfe::maxFetchSamples,
    .maximumFetchesPerStep = 4,
    .fetchWaitMs = 20,
    .task =
        {
            .name = "AudioAfe",
            .priority = 4,
            .core = Totem::TaskController::Config::CorePreference::specific(1),
            .stackSize = Totem::StaticConfig::TaskStacks::audioAfe,
            .intervalMs = 1,
            .noCatchup = true,
            .autoRestart = true,
        },
};

inline constexpr AiAudio::WakeSessionConfig wakeSessionConfig{
    .noSpeechTimeoutMs = 5000,
    .maximumSessionMs = 30000,
    .recordingStatus =
        {
            .name = "Recording",
            .color = {.red = 160, .green = 80, .blue = 0},
            .kind = Totem::StatusLed::StateKind::Warning,
        },
};

inline constexpr AiAudio::DelayedPlaybackConfig delayedPlaybackConfig{
    .sampleRate = AiAudio::nemoAsrPcmAudio.sampleRate,
    .delayMs = 1000,
};

inline constexpr Totem::AudioSink::I2SSinkConfig max98357LoopbackSinkConfig{
    .device = Totem::AudioSink::I2SSinkDevicePreset::Custom,
    .customLink =
        {
            .audio = AiAudio::nemoAsrPcmAudio,
            .hostClockRole = Totem::AudioSink::I2SHostClockRole::ProvidesClock,
            .format = Totem::AudioSink::I2SFormat::Philips,
            .channel = Totem::AudioSink::I2SChannelSelect::Left,
            .port = 1,
            .useApll = true,
        },
    .pins =
        {
            .bitClock = Pin::A5,
            .wordSelect = Pin::A4,
            .dataOut = Pin::A6,
        },
};

inline constexpr Totem::Wifi::Config wifiConfig{
    .mode = Totem::Wifi::Mode::Station,
    .station =
        Totem::Wifi::StationConfig{
            .credentials =
                {
                    .ssid = "dre-guest",
                    .passwordSecretName = "wifi-sta-pass",
                },
            .reconnect = Totem::StaticConfig::Wifi::defaultStationReconnect,
            .maxReconnectAttempts =
                Totem::StaticConfig::Wifi::defaultStationMaxReconnectAttempts,
        },
    .disableNvsStorage = true,
};
