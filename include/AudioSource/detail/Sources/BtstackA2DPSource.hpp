#pragma once

#include "AudioSource/Interfaces/SourceConfig.hpp"
#include "AudioSource/Interfaces/Types.hpp"
#include "AudioSource/detail/PlatformSelect.hpp"
#include "AudioSource/detail/Sources/IAudioSource.hpp"
#include "AudioSource/detail/Sources/PcmRingStream.hpp"
#include "AudioSource/detail/Types.hpp"
#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {
#include "bluetooth.h"
#include "bluetooth_company_id.h"
#include "bluetooth_sdp.h"
#include "btstack_defines.h"
#include "btstack_event.h"
#include "btstack_port_esp32.h"
#include "btstack_run_loop.h"
#include "classic/a2dp_sink.h"
#include "classic/avdtp.h"
#include "classic/avdtp_util.h"
#include "classic/btstack_sbc.h"
#include "classic/device_id_server.h"
#include "classic/sdp_server.h"
#include "gap.h"
#include "hci.h"
#include "l2cap.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

namespace Totem::AudioSource::detail {

class BtstackA2DPSource
    : public HasLifecycle<BtstackA2DPSource, BtstackA2DPSourceConfig>,
      public IAudioSource {
    friend class HasLifecycle<BtstackA2DPSource, BtstackA2DPSourceConfig>;
    friend struct LifecycleContract<BtstackA2DPSource,
                                    BtstackA2DPSourceConfig>;

  public:
    DELETE_COPY(BtstackA2DPSource)
    DELETE_MOVE(BtstackA2DPSource)

    static constexpr const char *name = "AudioSource::BtstackA2DPSource";
    static constexpr LogComponent logComponent =
        Totem::AudioSource::detail::logComponent;

    BtstackA2DPSource() = default;

    [[nodiscard]] bool active() const override {
        return HasLifecycle<BtstackA2DPSource, BtstackA2DPSourceConfig>::
            active();
    }
    [[nodiscard]] const AudioInfo &audioInfo() const override {
        return _audioInfo;
    }
    [[nodiscard]] bool ready() const override {
        return _ready.load(std::memory_order_acquire);
    }
    [[nodiscard]] const char *sourceName() const override {
        return "btstack-a2dp";
    }

    bool pollReadiness(uint32_t nowMs) override {
        const auto available = _stream.bytesAvailable();
        const bool hasData = available >= config().bufferStartThresholdBytes ||
                             (ready() && available > 0);
        const bool isReady =
            _connected.load(std::memory_order_acquire) &&
            _streaming.load(std::memory_order_acquire) && hasData;
        _ready.store(isReady, std::memory_order_release);
        if (isReady) {
            return true;
        }

        if (_lastWaitingLogMs == 0 ||
            nowMs - _lastWaitingLogMs >= config().waitingLogIntervalMs) {
            _lastWaitingLogMs = nowMs;
            _log_w("Waiting for BTstack A2DP audio: connected=%u, "
                   "streaming=%u, buffer=%zu/%zu bytes",
                   _connected.load(std::memory_order_acquire) ? 1U : 0U,
                   _streaming.load(std::memory_order_acquire) ? 1U : 0U,
                   available, _stream.capacity());
        }
        return false;
    }

    void observeReadResult(std::size_t bytesRead, uint32_t nowMs) override {
        if (bytesRead > 0) {
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
    struct SbcConfiguration {
        uint8_t numChannels = 2;
        uint16_t sampleRate = 44100;
    };

    ReturnCode _onBegin() {
        FAIL_IF(_activeInstance != nullptr, ERR(CoreError, InvalidState),
                "Only one BTstack A2DP source instance can be active");

        _activeInstance = this;
        _audioInfo = config().audio;
        FAIL_IF_ERR_FWD(_stream.begin(_audioInfo),
                        "Failed to start BTstack A2DP PCM stream");

        _ready.store(false, std::memory_order_release);
        _connected.store(false, std::memory_order_release);
        _streaming.store(false, std::memory_order_release);
        _taskRunning.store(false, std::memory_order_release);
        _observedBytes.store(0, std::memory_order_release);
        _receivedBytes.store(0, std::memory_order_release);
        _lastDataMs.store(0, std::memory_order_release);
        _lastWaitingLogMs = 0;
        _lastCooperativeYieldTick = 0;
        _decoderConfigured = false;
        std::memset(&_decoderContext, 0, sizeof(_decoderContext));

        const auto created = xTaskCreatePinnedToCore(
            _taskEntry, "BtstackA2DP", config().taskStackSize, this,
            config().taskPriority, &_taskHandle, config().taskCore);
        FAIL_IF(created != pdPASS, ERR(CoreError, OperationFailed),
                "Failed to start BTstack A2DP task");

        _log_i("BTstack A2DP source starting: name=%s, %lu Hz, %u ch, %u bit",
               config().deviceName,
               static_cast<unsigned long>(_audioInfo.sampleRate),
               _audioInfo.channels, _audioInfo.bitsPerSample);
        return OK();
    }

    ReturnCode _onEnd() {
        _ready.store(false, std::memory_order_release);
        _connected.store(false, std::memory_order_release);
        _streaming.store(false, std::memory_order_release);
        if (_taskRunning.load(std::memory_order_acquire)) {
            btstack_run_loop_trigger_exit();
        }
        _stream.end();
        if (_activeInstance == this) {
            _activeInstance = nullptr;
        }
        return OK();
    }

    ReturnCode _setupBtstack() {
        l2cap_init();
        sdp_init();

        a2dp_sink_init();
        a2dp_sink_register_packet_handler(_onA2dpPacket);
        a2dp_sink_register_media_handler(_onMediaPacket);

        auto *localStreamEndpoint = a2dp_sink_create_stream_endpoint(
            AVDTP_AUDIO, AVDTP_CODEC_SBC, _sbcCodecCapabilities.data(),
            _sbcCodecCapabilities.size(), _sbcCodecConfiguration.data(),
            _sbcCodecConfiguration.size());
        FAIL_IF(localStreamEndpoint == nullptr, ERR(CoreError, OperationFailed),
                "Failed to create BTstack A2DP stream endpoint");
        _localSeid = avdtp_local_seid(localStreamEndpoint);

        _hciEventCallback.callback = _onHciPacket;
        hci_add_event_handler(&_hciEventCallback);

        gap_set_local_name(config().deviceName);
        gap_discoverable_control(1);
        gap_set_class_of_device(0x200404);
        gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
        gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH |
                                             LM_LINK_POLICY_ENABLE_SNIFF_MODE);
        gap_set_allow_role_switch(true);

        std::memset(_sdpA2dpSinkService.data(), 0,
                    _sdpA2dpSinkService.size());
        a2dp_sink_create_sdp_record(
            _sdpA2dpSinkService.data(), sdp_create_service_record_handle(),
            AVDTP_SINK_FEATURE_MASK_HEADPHONE, config().deviceName, nullptr);
        sdp_register_service(_sdpA2dpSinkService.data());

        std::memset(_sdpDeviceIdService.data(), 0,
                    _sdpDeviceIdService.size());
        device_id_create_sdp_record(
            _sdpDeviceIdService.data(), sdp_create_service_record_handle(),
            DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH,
            BLUETOOTH_COMPANY_ID_BLUEKITCHEN_GMBH, 1, 1);
        sdp_register_service(_sdpDeviceIdService.data());
        return OK();
    }

    void _run() {
        _taskRunning.store(true, std::memory_order_release);
        const auto initStatus = btstack_init();
        if (initStatus != ERROR_CODE_SUCCESS) {
            _log_e("BTstack init failed: status=0x%02x", initStatus);
            _taskRunning.store(false, std::memory_order_release);
            return;
        }

        auto setupRet = _setupBtstack();
        if (!setupRet.ok()) {
            _log_e("BTstack A2DP setup failed: " ERR_FMT,
                   ERR_ARG(setupRet));
            _taskRunning.store(false, std::memory_order_release);
            return;
        }

        _log_i("BTstack A2DP source started: name=%s, localSeid=%u",
               config().deviceName, _localSeid);
        hci_power_control(HCI_POWER_ON);
        btstack_run_loop_execute();

        _ready.store(false, std::memory_order_release);
        _connected.store(false, std::memory_order_release);
        _streaming.store(false, std::memory_order_release);
        _taskRunning.store(false, std::memory_order_release);
    }

    void _configureDecoder() {
        if (_decoderConfigured) {
            return;
        }
        btstack_sbc_decoder_init(&_decoderContext, SBC_MODE_STANDARD,
                                 _onPcmData, this);
        _decoderConfigured = true;
    }

    void _setSbcConfiguration(const uint8_t *packet) {
        _sbcConfiguration.numChannels =
            a2dp_subevent_signaling_media_codec_sbc_configuration_get_num_channels(
                packet);
        _sbcConfiguration.sampleRate =
            a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(
                packet);
        if (_sbcConfiguration.sampleRate != 0) {
            _audioInfo.sampleRate = _sbcConfiguration.sampleRate;
            Platform::setAudioInfo(_stream, _audioInfo);
        }
        _log_i("BTstack A2DP SBC config: %u Hz, %u decoded channels",
               _sbcConfiguration.sampleRate, _sbcConfiguration.numChannels);
    }

    void _setConnectionEstablished(const uint8_t *packet) {
        const auto status = a2dp_subevent_stream_established_get_status(packet);
        if (status != ERROR_CODE_SUCCESS) {
            _log_w("BTstack A2DP stream failed: status=0x%02x", status);
            return;
        }
        _a2dpCid = a2dp_subevent_stream_established_get_a2dp_cid(packet);
        _localSeid = a2dp_subevent_stream_established_get_local_seid(packet);
        _connected.store(true, std::memory_order_release);
        _lastWaitingLogMs = 0;
        _log_i("BTstack A2DP stream established: cid=0x%04x, localSeid=%u",
               _a2dpCid, _localSeid);
    }

    void _handleA2dpPacket(uint8_t packetType, const uint8_t *packet) {
        if (packetType != HCI_EVENT_PACKET ||
            hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) {
            return;
        }

        switch (packet[2]) {
        case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION:
            _setSbcConfiguration(packet);
            break;
        case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_OTHER_CONFIGURATION:
            _log_w("BTstack A2DP received non-SBC codec configuration");
            break;
        case A2DP_SUBEVENT_STREAM_ESTABLISHED:
            _setConnectionEstablished(packet);
            break;
        case A2DP_SUBEVENT_STREAM_STARTED:
            _configureDecoder();
            _streaming.store(true, std::memory_order_release);
            _log_i("BTstack A2DP stream started");
            break;
        case A2DP_SUBEVENT_STREAM_SUSPENDED:
            _streaming.store(false, std::memory_order_release);
            _ready.store(false, std::memory_order_release);
            _stream.clear();
            _lastWaitingLogMs = 0;
            _log_i("BTstack A2DP stream suspended");
            break;
        case A2DP_SUBEVENT_STREAM_RELEASED:
        case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
            _connected.store(false, std::memory_order_release);
            _streaming.store(false, std::memory_order_release);
            _ready.store(false, std::memory_order_release);
            _stream.clear();
            _lastWaitingLogMs = 0;
            _log_i("BTstack A2DP stream released");
            break;
        default:
            break;
        }
    }

    void _handleHciPacket(uint8_t packetType, uint8_t *packet) {
        if (packetType != HCI_EVENT_PACKET) {
            return;
        }

        switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                _log_i("BTstack A2DP Bluetooth controller is ready");
            }
            break;
        case HCI_EVENT_PIN_CODE_REQUEST: {
            bd_addr_t address;
            hci_event_pin_code_request_get_bd_addr(packet, address);
            gap_pin_code_response(address, "0000");
            break;
        }
        case HCI_EVENT_USER_CONFIRMATION_REQUEST: {
            bd_addr_t address;
            hci_event_user_confirmation_request_get_bd_addr(packet, address);
            gap_ssp_confirmation_response(address);
            break;
        }
        default:
            break;
        }
    }

    void _handleMediaPacket(uint8_t *packet, uint16_t size) {
        if (!_streaming.load(std::memory_order_acquire) || packet == nullptr ||
            size <= 13) {
            return;
        }

        constexpr uint16_t mediaHeaderBytes = 12;
        const uint8_t sbcHeader = packet[mediaHeaderBytes];
        const uint8_t frameCount = sbcHeader & 0x0FU;
        if (frameCount == 0) {
            return;
        }

        _configureDecoder();
        const auto *sbcData = packet + mediaHeaderBytes + 1;
        const auto sbcBytes = static_cast<uint16_t>(size - mediaHeaderBytes - 1);
        btstack_sbc_decoder_process_data(&_decoderContext, 0, sbcData,
                                         sbcBytes);
        _yieldCooperatively();
    }

    void _writeDecodedPcm(int16_t *data, int numSamples, int numChannels,
                          int sampleRate) {
        if (data == nullptr || numSamples <= 0 || numChannels <= 0) {
            return;
        }
        if (sampleRate > 0 &&
            static_cast<uint32_t>(sampleRate) != _audioInfo.sampleRate) {
            _audioInfo.sampleRate = static_cast<uint32_t>(sampleRate);
            Platform::setAudioInfo(_stream, _audioInfo);
        }

        const auto bytes = static_cast<std::size_t>(numSamples) *
                           static_cast<std::size_t>(numChannels) *
                           sizeof(int16_t);
        const auto *pcm = reinterpret_cast<const uint8_t *>(data);
        const auto written =
            _audioInfo.channels == 1 && numChannels == 2
                ? _stream.writeStereo16AsMono(pcm, bytes)
                : _stream.writePcm(pcm, bytes);
        if (written > 0) {
            _receivedBytes.fetch_add(static_cast<uint32_t>(written),
                                     std::memory_order_acq_rel);
        }
    }

    void _yieldCooperatively() {
        if (config().cooperativeYieldIntervalMs == 0) {
            return;
        }

        auto intervalTicks =
            pdMS_TO_TICKS(config().cooperativeYieldIntervalMs);
        if (intervalTicks == 0) {
            intervalTicks = 1;
        }

        const auto now = xTaskGetTickCount();
        if (_lastCooperativeYieldTick == 0 ||
            now - _lastCooperativeYieldTick >= intervalTicks) {
            _lastCooperativeYieldTick = now;
            vTaskDelay(1);
        }
    }

    static void _taskEntry(void *arg) {
        auto *self = static_cast<BtstackA2DPSource *>(arg);
        if (self != nullptr) {
            self->_run();
        }
        vTaskDelete(nullptr);
    }

    static void _onHciPacket(uint8_t packetType, uint16_t, uint8_t *packet,
                             uint16_t) {
        auto *self = _activeInstance;
        if (self != nullptr) {
            self->_handleHciPacket(packetType, packet);
        }
    }

    static void _onA2dpPacket(uint8_t packetType, uint16_t, uint8_t *packet,
                              uint16_t) {
        auto *self = _activeInstance;
        if (self != nullptr) {
            self->_handleA2dpPacket(packetType, packet);
        }
    }

    static void _onMediaPacket(uint8_t, uint8_t *packet, uint16_t size) {
        auto *self = _activeInstance;
        if (self != nullptr) {
            self->_handleMediaPacket(packet, size);
        }
    }

    static void _onPcmData(int16_t *data, int numSamples, int numChannels,
                           int sampleRate, void *context) {
        auto *self = static_cast<BtstackA2DPSource *>(context);
        if (self != nullptr) {
            self->_writeDecodedPcm(data, numSamples, numChannels, sampleRate);
        }
    }

    static inline BtstackA2DPSource *_activeInstance = nullptr;

    PcmRingStream _stream{};
    AudioInfo _audioInfo{};
    SbcConfiguration _sbcConfiguration{};
    btstack_sbc_decoder_state_t _decoderContext{};
    btstack_packet_callback_registration_t _hciEventCallback{};
    std::array<uint8_t, 150> _sdpA2dpSinkService{};
    std::array<uint8_t, 100> _sdpDeviceIdService{};
    std::array<uint8_t, 4> _sbcCodecConfiguration{};
    std::array<uint8_t, 4> _sbcCodecCapabilities{{0xFF, 0xFF, 2, 53}};
    TaskHandle_t _taskHandle = nullptr;
    uint16_t _a2dpCid = 0;
    uint8_t _localSeid = 0;
    bool _decoderConfigured = false;
    uint32_t _lastWaitingLogMs = 0;
    TickType_t _lastCooperativeYieldTick = 0;
    std::atomic<bool> _ready{false};
    std::atomic<bool> _connected{false};
    std::atomic<bool> _streaming{false};
    std::atomic<bool> _taskRunning{false};
    std::atomic<uint32_t> _observedBytes{0};
    std::atomic<uint32_t> _receivedBytes{0};
    std::atomic<uint32_t> _lastDataMs{0};
};

} // namespace Totem::AudioSource::detail
