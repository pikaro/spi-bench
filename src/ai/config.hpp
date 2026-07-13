#pragma once

#include "AudioSink/Facade.hpp"
#include "AudioSource/Interfaces/SourceConfig.hpp"
#include "Platform/Hardware.hpp"
#include "StaticConfig/Wifi.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "Wifi/Interfaces/Config.hpp"
#include "Wifi/Interfaces/Types.hpp"
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

inline constexpr Totem::AudioSink::I2SSinkConfig max98357LoopbackSinkConfig{
    .device = Totem::AudioSink::I2SSinkDevicePreset::Custom,
    .customLink =
        {
            .audio = AiAudio::nemoAsrPcmAudio,
            .hostClockRole =
                Totem::AudioSink::I2SHostClockRole::ProvidesClock,
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
