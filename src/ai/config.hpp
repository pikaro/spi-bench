#pragma once

#include "AudioAfe/Facade.hpp"
#include "AudioSink/Facade.hpp"
#include "AudioSource/Interfaces/SourceConfig.hpp"
#include "Platform/Hardware.hpp"
#include "StaticConfig/Wifi.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "Wifi/Interfaces/Config.hpp"
#include "Wifi/Interfaces/Types.hpp"
#include "assistant_session.hpp"
#include "pcm16_downsampler.hpp"
#include <array>

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
            .minimumSilenceMs = 320,
            .lookbackMs = 128,
            .mutePlayback = false,
            .enableChannelTrigger = false,
        },
    .wakeNet =
        {
            .enabled = true,
            .mode = Totem::AudioAfe::WakeNetMode::Normal,
            .primary =
                {
                    .modelName = "wn9_alexa",
                    .threshold = 0.0F,
                },
            .secondary =
                Totem::AudioAfe::WakeNetModelConfig{
                    .modelName = "wn9_computer_tts",
                    .threshold = 0.0F,
                },
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

inline constexpr char assistantTrustedRootPem[] =
    R"PEM(-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)PEM";

inline constexpr Totem::AudioSink::AudioInfo assistantResponsePcmAudio{
    .sampleRate = 24000,
    .channels = 1,
    .bitsPerSample = 16,
};

inline constexpr Totem::AudioSink::AudioInfo assistantPlaybackPcmAudio{
    .sampleRate = 32000,
    .channels = 1,
    .bitsPerSample = 16,
};

inline constexpr std::array<AiAudio::AssistantWakeProfile, 2>
    assistantWakeProfiles{{
        {
            .wakeNetModelIndex = 1,
            .wakeWordIndex = 1,
            .wakeWord = "Alexa",
            .voice = "attenborough",
        },
        {
            .wakeNetModelIndex = 2,
            .wakeWordIndex = 1,
            .wakeWord = "Computer",
            .voice = "bender",
        },
    }};

inline constexpr AiAudio::AssistantSessionConfig assistantSessionConfig{
    .webSocket =
        {
            .host = "hal.d-reis.com",
            .port = 443,
            .path = "/v1/realtime",
            .authorizationHeaderSecretName = "hal-auth",
            .trustedRootPem = assistantTrustedRootPem,
            .appendPcmBytes = 2048,
            .maximumInboundEventBytes = 128U * 1024U,
            .maximumDecodedAudioBytes = 96U * 1024U,
            .connectTimeoutMs = 10000,
            .writeTimeoutMs = 3000,
            .readTimeoutMs = 250,
        },
    .wakeProfiles = assistantWakeProfiles,
    .inputAudio = AiAudio::nemoAsrPcmAudio,
    .responseAudio = assistantResponsePcmAudio,
    .playbackAudio = assistantPlaybackPcmAudio,
    .noSpeechTimeoutMs = 5000,
    .maximumSessionMs = 30000,
    .captureCapacityBytes = 128000,
    .playbackWriteBytes = 4096,
    .playbackQueueCapacityBytes = 512U * 1024U,
    .playbackPrebufferBytes = 8U * 1024U,
    .playbackPrimeWriteBytes = 512,
    .playbackQueueWriteTimeoutMs = 1000,
    .playbackQueuePollMs = 10,
    .playbackCompletionTimeoutMs = 30000,
    .playbackDrainMarginMs = 10,
    .uploadPollMs = 10,
    .responseTimeoutMs = 120000,
    .playbackWriteTimeoutMs = 3000,
    .writerQuiesceTimeoutMs = 100,
    .playbackStartAtZeroCrossing = true,
    .responseGain = 1.0F,
    .sntpServer = "pool.ntp.org",
    .minimumValidEpoch = 1735689600,
    .sntpSyncTimeoutMs = 30000,
    .readinessPollMs = 250,
    .readinessLogIntervalMs = 10000,
    .taskName = "AiAssistant",
    .taskStackBytes = 12U * 1024U,
    .taskPriority = 4,
    .taskCore = 0,
    .playbackTaskName = "AiPlayback",
    .playbackTaskStackBytes = 4096,
    .playbackTaskPriority = 5,
    .playbackTaskCore = 0,
    .listeningStatus =
        {
            .name = "Listening",
            .color = {.red = 24, .green = 24, .blue = 24},
            .kind = Totem::StatusLed::StateKind::Informational,
        },
    .recordingStatus =
        {
            .name = "Recording",
            .color = {.red = 160, .green = 80, .blue = 0},
            .kind = Totem::StatusLed::StateKind::Warning,
        },
    .playbackStatus =
        {
            .name = "Playback",
            .color = {.red = 96, .green = 0, .blue = 128},
            .kind = Totem::StatusLed::StateKind::Informational,
        },
};

inline constexpr Totem::AudioSink::I2SSinkConfig max98357ResponseSinkConfig{
    .device = Totem::AudioSink::I2SSinkDevicePreset::Custom,
    .customLink =
        {
            .audio = assistantPlaybackPcmAudio,
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
