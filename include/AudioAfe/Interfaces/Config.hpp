#pragma once

#include "StaticConfig/AudioAfe.hpp"
#include "StaticConfig/Stacks.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include <cstddef>
#include <cstdint>

namespace Totem::AudioAfe {

enum class PerformanceMode : uint8_t { LowCost, HighPerformance };
enum class MemoryAllocation : uint8_t {
    PreferInternal,
    Balanced,
    PreferPsram,
};
enum class NoiseSuppressionMode : uint8_t { WebRtc, Neural };
enum class VadImplementation : uint8_t { WebRtc, Neural };
enum class VadMode : uint8_t {
    Normal,
    Aggressive,
    VeryAggressive,
    VeryVeryAggressive,
    Maximum,
};
enum class WakeNetMode : uint8_t { Normal, Aggressive };
enum class AgcMode : uint8_t { WebRtc, WakeNet };

struct NoiseSuppressionConfig {
    bool enabled = true;
    NoiseSuppressionMode mode = NoiseSuppressionMode::WebRtc;
    const char *modelName = nullptr;

    [[nodiscard]] bool validate() const {
        return enabled && (modelName == nullptr || modelName[0] != '\0');
    }
};

struct VadConfig {
    bool enabled = true;
    VadImplementation implementation = VadImplementation::Neural;
    VadMode mode = VadMode::Normal;
    const char *modelName = nullptr;
    uint16_t minimumSpeechMs = 128;
    uint16_t minimumSilenceMs = 800;
    uint16_t lookbackMs = 128;
    bool mutePlayback = false;
    bool enableChannelTrigger = false;

    [[nodiscard]] bool validate() const {
        return enabled && (modelName == nullptr || modelName[0] != '\0') &&
               minimumSpeechMs > 32 && minimumSilenceMs > 64 &&
               lookbackMs <= 2000 && !enableChannelTrigger;
    }
};

struct WakeNetConfig {
    bool enabled = true;
    const char *modelName = nullptr;
    WakeNetMode mode = WakeNetMode::Normal;
    float threshold = 0.0F;
    uint8_t modelIndex = 1;

    [[nodiscard]] bool validate() const {
        const bool thresholdValid =
            threshold == 0.0F || (threshold >= 0.4F && threshold < 1.0F);
        return enabled && (modelName == nullptr || modelName[0] != '\0') &&
               thresholdValid && modelIndex >= 1 && modelIndex <= 2;
    }
};

struct AgcConfig {
    bool enabled = true;
    AgcMode mode = AgcMode::WakeNet;
    uint8_t compressionGainDb = 9;
    uint8_t targetLevelDbfs = 3;
    float linearGain = 1.0F;

    [[nodiscard]] bool validate() const {
        return enabled && compressionGainDb <= 90 && targetLevelDbfs <= 31 &&
               linearGain >= 0.1F && linearGain <= 10.0F;
    }
};

struct Config {
    const char *modelPartition = "model";
    PerformanceMode performance = PerformanceMode::HighPerformance;
    MemoryAllocation memory = MemoryAllocation::PreferPsram;
    NoiseSuppressionConfig noiseSuppression{};
    VadConfig vad{};
    WakeNetConfig wakeNet{};
    AgcConfig agc{};
    bool acousticEchoCancellation = false;
    bool speechEnhancement = false;
    uint8_t afeCore = 1;
    uint8_t afePriority = 5;
    uint8_t afeRingBufferFrames = 50;
    std::size_t maximumFeedSamples = StaticConfig::AudioAfe::maxFeedSamples;
    std::size_t maximumFetchSamples = StaticConfig::AudioAfe::maxFetchSamples;
    uint8_t maximumFetchesPerStep = 4;
    uint16_t fetchWaitMs = 20;
    TaskController::Config task{
        .name = "AudioAfe",
        .priority = 4,
        .core = TaskController::Config::CorePreference::specific(1),
        .stackSize = StaticConfig::TaskStacks::audioAfe,
        .intervalMs = 1,
        .noCatchup = true,
        .autoRestart = true,
    };

    [[nodiscard]] bool validate() const {
        // ESP-SR replaces WebRTC AGC with its WakeNet AGC whenever WakeNet is
        // active. Require the effective mode explicitly instead of silently
        // accepting a different pipeline than the caller configured.
        return modelPartition != nullptr && modelPartition[0] != '\0' &&
               noiseSuppression.validate() && vad.validate() &&
               wakeNet.validate() && agc.validate() &&
               !acousticEchoCancellation && !speechEnhancement &&
               afeCore <= 1 && afePriority > 0 && afePriority < 25 &&
               afeRingBufferFrames >= 2 && maximumFeedSamples > 0 &&
               maximumFeedSamples <= StaticConfig::AudioAfe::maxFeedSamples &&
               maximumFetchSamples > 0 &&
               maximumFetchSamples <= StaticConfig::AudioAfe::maxFetchSamples &&
               maximumFetchesPerStep > 0 && fetchWaitMs <= 100 &&
               agc.mode == AgcMode::WakeNet && task.validate() &&
               task.stackSize <= StaticConfig::TaskStacks::audioAfe;
    }
};

} // namespace Totem::AudioAfe
