#pragma once

#include "AudioSink/Facade.hpp"
#include "AudioSource/Interfaces/SourceConfig.hpp"
#include "Platform/Hardware.hpp"
#include "StaticConfig/Wifi.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "Wifi/Interfaces/Config.hpp"
#include "Wifi/Interfaces/Types.hpp"
#include "wifi_credentials.hpp"

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

inline constexpr Totem::Wifi::Config wifiConfig{
    .mode = Totem::Wifi::Mode::Station,
    .station =
        Totem::Wifi::StationConfig{
            .credentials =
                {
                    .ssid = MasterWifiCredentials::Station::ssid,
                    .passwordSecretName = "wifi-sta-pass",
                },
            .reconnect = Totem::StaticConfig::Wifi::defaultStationReconnect,
            .maxReconnectAttempts =
                Totem::StaticConfig::Wifi::defaultStationMaxReconnectAttempts,
        },
    .disableNvsStorage = true,
};
