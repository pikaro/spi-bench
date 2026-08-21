#pragma once

#include "AudioAfe/Interfaces/Config.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_nsn_models.h"
#include "esp_vadn_models.h"
#include "esp_wn_models.h"
#include "model_path.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace Totem::AudioAfe::detail::platform {

class PlatformESP32 {
  public:
    DELETE_COPY(PlatformESP32)
    DELETE_MOVE(PlatformESP32)

    PlatformESP32() = default;

    ReturnCode begin(const Config &config) {
        FAIL_IF(_afe != nullptr || _models != nullptr,
                ERR(CoreError, InvalidState),
                "ESP-SR AFE platform is already active");

        _models = esp_srmodel_init(config.modelPartition);
        if (_models == nullptr) {
            return _beginFailure(ERR(CoreError, NotFound),
                                 "Failed to load ESP-SR model partition");
        }

        _config = afe_config_init("M", _models, AFE_TYPE_SR,
                                  config.performance ==
                                          PerformanceMode::HighPerformance
                                      ? AFE_MODE_HIGH_PERF
                                      : AFE_MODE_LOW_COST);
        if (_config == nullptr) {
            return _beginFailure(ERR(CoreError, OutOfMemory),
                                 "Failed to allocate ESP-SR AFE config");
        }

        _wakeModels[0] =
            _resolveModel(config.wakeNet.primary.modelName, ESP_WN_PREFIX);
        if (_wakeModels[0] == nullptr) {
            return _beginFailure(ERR(CoreError, NotFound),
                                 "Configured primary WakeNet model is unavailable");
        }
        if (config.wakeNet.secondary.has_value()) {
            _wakeModels[1] = _resolveModel(
                config.wakeNet.secondary->modelName, ESP_WN_PREFIX);
        }
        if (config.wakeNet.secondary.has_value() &&
            _wakeModels[1] == nullptr) {
            return _beginFailure(
                ERR(CoreError, NotFound),
                "Configured secondary WakeNet model is unavailable");
        }

        _vadModel = config.vad.implementation == VadImplementation::Neural
                        ? _resolveModel(config.vad.modelName, ESP_VADN_PREFIX)
                        : nullptr;
        if (config.vad.implementation == VadImplementation::Neural &&
            _vadModel == nullptr) {
            return _beginFailure(ERR(CoreError, NotFound),
                                 "Configured VADNet model is unavailable");
        }

        _nsModel = config.noiseSuppression.mode == NoiseSuppressionMode::Neural
                       ? _resolveModel(config.noiseSuppression.modelName,
                                       ESP_NSNET_PREFIX)
                       : nullptr;
        if (config.noiseSuppression.mode == NoiseSuppressionMode::Neural &&
            _nsModel == nullptr) {
            return _beginFailure(ERR(CoreError, NotFound),
                                 "Configured NSNet model is unavailable");
        }

        _applyConfig(config);
        auto *checked = afe_config_check(_config);
        if (checked == nullptr) {
            return _beginFailure(ERR(CoreError, InvalidArgument),
                                 "ESP-SR rejected the AFE config");
        }
        _config = checked;
        const auto requestedAgcMode = config.agc.mode == AgcMode::WebRtc
                                          ? AFE_AGC_MODE_WEBRTC
                                          : AFE_AGC_MODE_WAKENET;
        if (!_config->wakenet_init || !_config->vad_init ||
            !_config->agc_init || _config->agc_mode != requestedAgcMode ||
            !_config->ns_init || _config->vad_enable_channel_trigger) {
            return _beginFailure(
                ERR(CoreError, InvalidState),
                "Effective ESP-SR pipeline violates the AI audio policy");
        }

        _iface = esp_afe_handle_from_config(_config);
        if (_iface == nullptr) {
            return _beginFailure(ERR(CoreError, NotFound),
                                 "No ESP-SR AFE implementation for config");
        }
        _afe = _iface->create_from_config(_config);
        if (_afe == nullptr) {
            return _beginFailure(ERR(CoreError, OutOfMemory),
                                 "Failed to create ESP-SR AFE");
        }

        const std::array<const WakeNetModelConfig *, 2> wakeModels{
            &config.wakeNet.primary,
            config.wakeNet.secondary.has_value()
                ? &config.wakeNet.secondary.value()
                : nullptr,
        };
        for (std::size_t index = 0; index < wakeModels.size(); ++index) {
            const auto *model = wakeModels[index];
            if (model == nullptr || model->threshold == 0.0F) {
                continue;
            }
            if (_iface->set_wakenet_threshold(
                    _afe, static_cast<int>(index + 1U), model->threshold) !=
                1) {
                return _beginFailure(ERR(CoreError, OperationFailed),
                                     "Failed to set WakeNet threshold");
            }
        }

        const auto feedChunk = _iface->get_feed_chunksize(_afe);
        const auto feedChannels = _iface->get_feed_channel_num(_afe);
        const auto fetchChunk = _iface->get_fetch_chunksize(_afe);
        const auto fetchChannels = _iface->get_fetch_channel_num(_afe);
        const auto sampleRate = _iface->get_samp_rate(_afe);
        if (feedChunk <= 0 || feedChannels <= 0 || fetchChunk <= 0 ||
            fetchChannels <= 0 || sampleRate != 16000) {
            return _beginFailure(ERR(CoreError, InvalidState),
                                 "Invalid ESP-SR AFE frame geometry");
        }

        _feedSamples = static_cast<std::size_t>(feedChunk) *
                       static_cast<std::size_t>(feedChannels);
        _fetchSamples = static_cast<std::size_t>(fetchChunk) *
                        static_cast<std::size_t>(fetchChannels);
        if (_feedSamples > config.maximumFeedSamples ||
            _fetchSamples > config.maximumFetchSamples) {
            return _beginFailure(ERR(CoreError, Overflow),
                                 "ESP-SR frame exceeds configured capacity");
        }

        _log_i("ESP-SR ready: models=%d wake1=%s wake2=%s vad=%s ns=%s "
               "feed=%u fetch=%u rate=%dHz",
               _models->num, _wakeModels[0],
               _wakeModels[1] == nullptr ? "none" : _wakeModels[1],
               _vadModel == nullptr ? "webrtc" : _vadModel,
               _nsModel == nullptr ? "webrtc" : _nsModel,
               static_cast<unsigned>(_feedSamples),
               static_cast<unsigned>(_fetchSamples), sampleRate);
        _iface->print_pipeline(_afe);
        return OK();
    }

    ReturnCode end() {
        if (_afe != nullptr && _iface != nullptr) {
            _iface->destroy(_afe);
        }
        _afe = nullptr;
        _iface = nullptr;
        if (_config != nullptr) {
            afe_config_free(_config);
        }
        _config = nullptr;
        if (_models != nullptr) {
            esp_srmodel_deinit(_models);
        }
        _models = nullptr;
        _wakeModels.fill(nullptr);
        _vadModel = nullptr;
        _nsModel = nullptr;
        _feedSamples = 0;
        _fetchSamples = 0;
        return OK();
    }

    [[nodiscard]] std::size_t feedSamples() const { return _feedSamples; }
    [[nodiscard]] std::size_t fetchSamples() const { return _fetchSamples; }

    ReturnCode feed(const int16_t *samples) {
        FAIL_IF(_afe == nullptr || _iface == nullptr,
                ERR(CoreError, InvalidState), "ESP-SR AFE is not active");
        FAIL_IF_NULL(samples, ERR(CoreError, InvalidArgument),
                     "ESP-SR feed samples are null");
        const auto accepted = _iface->feed(_afe, samples);
        FAIL_IF(accepted < 0, ERR(CoreError, OperationFailed),
                "ESP-SR feed failed with %d", accepted);
        return OK();
    }

    afe_fetch_result_t *fetch(TickType_t ticksToWait) {
        if (_afe == nullptr || _iface == nullptr) {
            return nullptr;
        }
        return _iface->fetch_with_delay(_afe, ticksToWait);
    }

  private:
    ReturnCode _beginFailure(ReturnCode error, const char *message) {
        _log_e("%s: " ERR_FMT, message, ERR_ARG(error));
        (void)end();
        return error;
    }

    char *_resolveModel(const char *configured, const char *prefix) const {
        if (configured == nullptr) {
            return esp_srmodel_filter(_models, prefix, nullptr);
        }
        auto *requested = const_cast<char *>(configured);
        if (esp_srmodel_exists(_models, requested) >= 0) {
            return requested;
        }
        return esp_srmodel_filter(_models, prefix, configured);
    }

    void _applyConfig(const Config &config) {
        _config->aec_init = config.acousticEchoCancellation;
        _config->se_init = config.speechEnhancement;

        _config->ns_init = config.noiseSuppression.enabled;
        _config->afe_ns_mode =
            config.noiseSuppression.mode == NoiseSuppressionMode::WebRtc
                ? AFE_NS_MODE_WEBRTC
                : AFE_NS_MODE_NET;
        _config->ns_model_name = _nsModel;

        _config->vad_init = config.vad.enabled;
        _config->vad_mode = static_cast<vad_mode_t>(config.vad.mode);
        _config->vad_model_name = _vadModel;
        _config->vad_min_speech_ms = config.vad.minimumSpeechMs;
        _config->vad_min_noise_ms = config.vad.minimumSilenceMs;
        _config->vad_delay_ms = config.vad.lookbackMs;
        _config->vad_mute_playback = config.vad.mutePlayback;
        _config->vad_enable_channel_trigger = config.vad.enableChannelTrigger;

        _config->wakenet_init = config.wakeNet.enabled;
        _config->wakenet_model_name = _wakeModels[0];
        _config->wakenet_model_name_2 = _wakeModels[1];
        _config->wakenet_mode = config.wakeNet.mode == WakeNetMode::Normal
                                    ? DET_MODE_90
                                    : DET_MODE_95;

        _config->agc_init = config.agc.enabled;
        _config->agc_mode = config.agc.mode == AgcMode::WebRtc
                                ? AFE_AGC_MODE_WEBRTC
                                : AFE_AGC_MODE_WAKENET;
        _config->agc_compression_gain_db = config.agc.compressionGainDb;
        _config->agc_target_level_dbfs = config.agc.targetLevelDbfs;

        _config->afe_perferred_core = config.afeCore;
        _config->afe_perferred_priority = config.afePriority;
        _config->afe_ringbuf_size = config.afeRingBufferFrames;
        _config->memory_alloc_mode =
            config.memory == MemoryAllocation::PreferInternal
                ? AFE_MEMORY_ALLOC_MORE_INTERNAL
            : config.memory == MemoryAllocation::Balanced
                ? AFE_MEMORY_ALLOC_INTERNAL_PSRAM_BALANCE
                : AFE_MEMORY_ALLOC_MORE_PSRAM;
        _config->afe_linear_gain = config.agc.linearGain;
        _config->debug_init = false;
        _config->fixed_first_channel = true;
        _config->fixed_output_channel = true;
        _config->output_playback_channel = false;
    }

    srmodel_list_t *_models = nullptr;
    afe_config_t *_config = nullptr;
    const esp_afe_sr_iface_t *_iface = nullptr;
    esp_afe_sr_data_t *_afe = nullptr;
    std::array<char *, 2> _wakeModels{};
    char *_vadModel = nullptr;
    char *_nsModel = nullptr;
    std::size_t _feedSamples = 0;
    std::size_t _fetchSamples = 0;

    static constexpr LogComponent logComponent = LogComponent::Audio;
};

} // namespace Totem::AudioAfe::detail::platform
