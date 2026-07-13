#pragma once

#include "AudioSource/Interfaces/SourceConfig.hpp"
#include "AudioSource/Interfaces/Types.hpp"
#include "AudioSource/detail/PlatformSelect.hpp"
#include "AudioSource/detail/Sources/IAudioSource.hpp"
#include "AudioSource/detail/Sources/PcmRingStream.hpp"
#include "AudioSource/detail/Types.hpp"
#include "Base/HasLifecycle.hpp"
#include "BluetoothA2DPSink.h"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Totem::AudioSource::detail {

class A2DPSource : public HasLifecycle<A2DPSource, A2DPSourceConfig>,
                   public IAudioSource {
    friend class HasLifecycle<A2DPSource, A2DPSourceConfig>;
    friend struct LifecycleContract<A2DPSource, A2DPSourceConfig>;

  public:
    DELETE_COPY(A2DPSource)
    DELETE_MOVE(A2DPSource)

    static constexpr const char *name = "AudioSource::A2DPSource";
    static constexpr LogComponent logComponent =
        Totem::AudioSource::detail::logComponent;

    A2DPSource() = default;

    [[nodiscard]] bool active() const override {
        return HasLifecycle<A2DPSource, A2DPSourceConfig>::active();
    }
    [[nodiscard]] const AudioInfo &audioInfo() const override {
        return _audioInfo;
    }
    [[nodiscard]] bool ready() const override {
        return _ready.load(std::memory_order_acquire);
    }
    [[nodiscard]] const char *sourceName() const override { return "a2dp"; }

    bool pollReadiness(uint32_t nowMs) override {
        const auto available = _stream.bytesAvailable();
        const bool hasData = available >= config().bufferStartThresholdBytes ||
                             (ready() && available > 0);
        const bool isReady =
            _connected.load(std::memory_order_acquire) && hasData;
        _ready.store(isReady, std::memory_order_release);
        if (isReady) {
            _lastWaitingLogMs = 0;
            return true;
        }

        if (_lastWaitingLogMs == 0 ||
            nowMs - _lastWaitingLogMs >= config().waitingLogIntervalMs) {
            _lastWaitingLogMs = nowMs;
            _log_w("Waiting for A2DP audio: connected=%u, streaming=%u, "
                   "buffer=%zu/%zu bytes",
                   _connected.load(std::memory_order_acquire) ? 1U : 0U,
                   _streaming.load(std::memory_order_acquire) ? 1U : 0U,
                   available, _stream.capacity());
        }
        return false;
    }

    void observeReadResult(std::size_t bytesRead, uint32_t nowMs) override {
        if (bytesRead > 0) {
            _lastWaitingLogMs = 0;
            _observedBytes.fetch_add(static_cast<uint32_t>(bytesRead),
                                     std::memory_order_acq_rel);
            _lastDataMs.store(nowMs, std::memory_order_release);
            return;
        }
        if (_stream.bytesAvailable() == 0) {
            _ready.store(false, std::memory_order_release);
        }
    }

    Platform::AudioStream &stream() override { return _stream; }

  private:
    ReturnCode _onBegin() {
        FAIL_IF(_activeInstance != nullptr, ERR(CoreError, InvalidState),
                "Only one A2DP source instance can be active");

        _activeInstance = this;
        _audioInfo = config().audio;
        FAIL_IF_ERR_FWD(_stream.begin(_audioInfo),
                        "Failed to start A2DP PCM stream");

        _sink.set_task_core(config().taskCore);
        _sink.set_task_priority(config().taskPriority);
        _sink.set_sample_rate_callback(_onSampleRate);
        _sink.set_on_audio_state_changed(_onAudioStateChanged, this);
        _sink.set_on_connection_state_changed(_onConnectionStateChanged, this);
        _sink.set_output_active(false);
        if (config().readerMode == A2DPReaderMode::Raw) {
            _sink.set_raw_stream_reader(_onPcmData);
        } else {
            _sink.set_stream_reader(_onPcmData, false);
        }
        _sink.start(config().deviceName);

        _ready.store(false, std::memory_order_release);
        _connected.store(false, std::memory_order_release);
        _streaming.store(false, std::memory_order_release);
        _observedBytes.store(0, std::memory_order_release);
        _receivedBytes.store(0, std::memory_order_release);
        _lastDataMs.store(0, std::memory_order_release);
        _lastWaitingLogMs = 0;
        _log_i("A2DP source started: name=%s, mode=%s, %lu Hz, %u ch, %u bit",
               config().deviceName,
               config().readerMode == A2DPReaderMode::Raw ? "raw" : "stream",
               static_cast<unsigned long>(_audioInfo.sampleRate),
               _audioInfo.channels, _audioInfo.bitsPerSample);
        return OK();
    }

    ReturnCode _onEnd() {
        _ready.store(false, std::memory_order_release);
        _connected.store(false, std::memory_order_release);
        _streaming.store(false, std::memory_order_release);
        _sink.end(false);
        _stream.end();
        if (_activeInstance == this) {
            _activeInstance = nullptr;
        }
        return OK();
    }

    void _receivePcm(const uint8_t *data, uint32_t len) {
        const auto written =
            _audioInfo.channels == 1 ? _stream.writeStereo16AsMono(data, len)
                                     : _stream.writePcm(data, len);
        if (written > 0) {
            _receivedBytes.fetch_add(static_cast<uint32_t>(written),
                                     std::memory_order_acq_rel);
        }
    }

    void _setSampleRate(uint16_t sampleRate) {
        if (sampleRate == 0) {
            return;
        }
        _audioInfo.sampleRate = sampleRate;
        Platform::setAudioInfo(_stream, _audioInfo);
        _log_i("A2DP sample rate updated: %u Hz", sampleRate);
    }

    void _setAudioState(esp_a2d_audio_state_t state) {
        const bool streaming = state == ESP_A2D_AUDIO_STATE_STARTED;
        _streaming.store(streaming, std::memory_order_release);
        if (!streaming) {
            _ready.store(false, std::memory_order_release);
        }
        _log_i("A2DP audio state changed: %d", static_cast<int>(state));
    }

    void _setConnectionState(esp_a2d_connection_state_t state) {
        const bool connected = state == ESP_A2D_CONNECTION_STATE_CONNECTED;
        _connected.store(connected, std::memory_order_release);
        if (!connected) {
            _ready.store(false, std::memory_order_release);
            _stream.clear();
        }
        _log_i("A2DP connection state changed: %d", static_cast<int>(state));
    }

    static void _onPcmData(const uint8_t *data, uint32_t len) {
        auto *self = _activeInstance;
        if (self == nullptr) {
            return;
        }
        self->_receivePcm(data, len);
    }

    static void _onSampleRate(uint16_t sampleRate) {
        auto *self = _activeInstance;
        if (self == nullptr) {
            return;
        }
        self->_setSampleRate(sampleRate);
    }

    static void _onAudioStateChanged(esp_a2d_audio_state_t state,
                                     void *owner) {
        auto *self = static_cast<A2DPSource *>(owner);
        if (self == nullptr) {
            return;
        }
        self->_setAudioState(state);
    }

    static void
    _onConnectionStateChanged(esp_a2d_connection_state_t state,
                              void *owner) {
        auto *self = static_cast<A2DPSource *>(owner);
        if (self == nullptr) {
            return;
        }
        self->_setConnectionState(state);
    }

    static inline A2DPSource *_activeInstance = nullptr;

    BluetoothA2DPSink _sink{};
    PcmRingStream _stream{};
    AudioInfo _audioInfo{};
    uint32_t _lastWaitingLogMs = 0;
    std::atomic<bool> _ready{false};
    std::atomic<bool> _connected{false};
    std::atomic<bool> _streaming{false};
    std::atomic<uint32_t> _observedBytes{0};
    std::atomic<uint32_t> _receivedBytes{0};
    std::atomic<uint32_t> _lastDataMs{0};
};

} // namespace Totem::AudioSource::detail
