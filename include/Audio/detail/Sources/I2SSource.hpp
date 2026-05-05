#pragma once

#include "Audio/Interfaces/SourceConfig.hpp"
#include "Audio/Interfaces/Types.hpp"
#include "Audio/detail/PlatformSelect.hpp"
#include "Audio/detail/Sources/IAudioSource.hpp"
#include "Audio/detail/Types.hpp"
#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Totem::Audio::detail {

class I2SSource : public HasLifecycle<I2SSource, I2SSourceConfig>,
                  public IAudioSource {
    friend class HasLifecycle<I2SSource, I2SSourceConfig>;
    friend struct LifecycleContract<I2SSource, I2SSourceConfig>;

  public:
    DELETE_COPY(I2SSource)
    DELETE_MOVE(I2SSource)

    static constexpr const char *name = "Audio::I2SSource";
    static constexpr LogComponent logComponent =
        Totem::Audio::detail::logComponent;

    I2SSource() = default;

    [[nodiscard]] bool active() const override {
        return HasLifecycle<I2SSource, I2SSourceConfig>::active();
    }
    [[nodiscard]] const AudioInfo &audioInfo() const override {
        return _audioInfo;
    }
    [[nodiscard]] const I2SLinkConfig &linkConfig() const {
        return _linkConfig;
    }
    [[nodiscard]] bool ready() const override {
        return _ready.load(std::memory_order_acquire);
    }
    [[nodiscard]] const char *sourceName() const override { return "i2s"; }
    [[nodiscard]] uint32_t readinessProbeCount() const override {
        return _probes.load(std::memory_order_acquire);
    }
    [[nodiscard]] I2SSourceStatus status() const {
        return I2SSourceStatus{
            .ready = ready(),
            .probes = _probes.load(std::memory_order_acquire),
            .emptyReads = _emptyReads.load(std::memory_order_acquire),
            .readTimeouts = _readTimeouts.load(std::memory_order_acquire),
            .readErrors = _readErrors.load(std::memory_order_acquire),
            .observedBytes = _observedBytes.load(std::memory_order_acquire),
            .lastDataMs = _lastDataMs.load(std::memory_order_acquire),
            .lastReadStatus = _lastReadStatus.load(std::memory_order_acquire),
        };
    }

    Platform::AudioStream &stream() override { return _input.stream(); }

    bool pollReadiness(uint32_t nowMs) override {
        if (!_input.active()) {
            return false;
        }
        if (ready()) {
            return true;
        }
        if (_lastProbeMs != 0 &&
            nowMs - _lastProbeMs < this->config().readiness.probeIntervalMs) {
            return false;
        }

        _lastProbeMs = nowMs;
        _probes.fetch_add(1, std::memory_order_acq_rel);
        const auto bytesRead = _input.readBytes(
            _probeBuffer.data(), this->config().readiness.probeBytes);
        observeReadResult(bytesRead, nowMs);
        return bytesRead > 0;
    }

    void observeReadResult(size_t bytesRead, uint32_t nowMs) override {
        _lastReadStatus.store(_input.lastReadStatusCode(),
                              std::memory_order_release);
        if (bytesRead > 0) {
            _markDataAvailable(bytesRead, nowMs);
            return;
        }

        _emptyReads.fetch_add(1, std::memory_order_acq_rel);
        if (_input.lastReadTimedOut()) {
            _readTimeouts.fetch_add(1, std::memory_order_acq_rel);
        } else if (!_input.lastReadOk()) {
            _readErrors.fetch_add(1, std::memory_order_acq_rel);
        }

        if (!ready()) {
            _logWaitingForData(nowMs);
            return;
        }

        ++_consecutiveEmptyReads;
        if (_consecutiveEmptyReads >=
            this->config().readiness.emptyReadsBeforeOffline) {
            _consecutiveEmptyReads = 0;
            _ready.store(false, std::memory_order_release);
            _lastWaitingLogMs = 0;
            _log_w("I2S source stopped producing data (%s/%ld); entering "
                   "probe mode",
                   _input.lastReadStatusName(),
                   static_cast<long>(_input.lastReadStatusCode()));
        }
    }

  private:
    ReturnCode _onBegin() {
        _linkConfig = this->config().resolvedLink();
        const auto &pins = this->config().pins;
        _log_i("Starting I2S input: %lu Hz, %u ch, %u bit, pins bck=%d ws=%d "
               "data=%d, hostClock=%s, format=%s, channel=%s",
               static_cast<unsigned long>(_linkConfig.audio.sampleRate),
               _linkConfig.audio.channels, _linkConfig.audio.bitsPerSample,
               static_cast<int>(pins.bitClock), static_cast<int>(pins.wordSelect),
               static_cast<int>(pins.dataIn),
               _hostClockRoleName(_linkConfig.hostClockRole),
               _formatName(_linkConfig.format),
               _channelName(_linkConfig.channel));
        FAIL_IF_ERR_FWD(_input.begin(pins, _linkConfig),
                        "Failed to start I2S input");
        FAIL_IF_ERR_FWD(
            _input.setReadTimeoutMs(this->config().readiness.readTimeoutMs),
            "Failed to configure I2S read timeout");
        _audioInfo = _input.audioInfo();
        _resetReadiness();
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = _input.end();
        _audioInfo = {};
        _resetReadiness();
        return ret;
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void _markDataAvailable(size_t bytesRead, uint32_t nowMs) {
        _consecutiveEmptyReads = 0;
        _observedBytes.fetch_add(static_cast<uint32_t>(bytesRead),
                                 std::memory_order_acq_rel);
        _lastDataMs.store(nowMs, std::memory_order_release);
        _lastWaitingLogMs = 0;
        const auto wasReady = _ready.exchange(true, std::memory_order_acq_rel);
        if (!wasReady && _shouldLogDataAvailable(nowMs)) {
            _log_i("I2S source is producing data after %lu probe(s)",
                   static_cast<unsigned long>(
                       _probes.load(std::memory_order_acquire)));
        }
    }

    void _logWaitingForData(uint32_t nowMs) {
        if (_lastWaitingLogMs != 0 &&
            nowMs - _lastWaitingLogMs <
                this->config().readiness.waitingLogIntervalMs) {
            return;
        }
        _lastWaitingLogMs = nowMs;

        if (_input.lastReadTimedOut()) {
            _log_w("Waiting for I2S data: read timed out after %u ms "
                   "(%s/%ld). pins bck=%d ws=%d data=%d, hostClock=%s, "
                   "format=%s, channel=%s. Check BCLK/WS clock, device power, "
                   "host/device clock role, and pin mapping.",
                   this->config().readiness.readTimeoutMs,
                   _input.lastReadStatusName(),
                   static_cast<long>(_input.lastReadStatusCode()),
                   static_cast<int>(this->config().pins.bitClock),
                   static_cast<int>(this->config().pins.wordSelect),
                   static_cast<int>(this->config().pins.dataIn),
                   _hostClockRoleName(_linkConfig.hostClockRole),
                   _formatName(_linkConfig.format),
                   _channelName(_linkConfig.channel));
            return;
        }

        if (!_input.lastReadOk()) {
            _log_e("I2S read failed while waiting for data: %s/%ld. pins "
                   "bck=%d ws=%d data=%d, hostClock=%s, format=%s, "
                   "channel=%s",
                   _input.lastReadStatusName(),
                   static_cast<long>(_input.lastReadStatusCode()),
                   static_cast<int>(this->config().pins.bitClock),
                   static_cast<int>(this->config().pins.wordSelect),
                   static_cast<int>(this->config().pins.dataIn),
                   _hostClockRoleName(_linkConfig.hostClockRole),
                   _formatName(_linkConfig.format),
                   _channelName(_linkConfig.channel));
            return;
        }

        _log_w("Waiting for I2S data: empty read without driver error. pins "
               "bck=%d ws=%d data=%d, hostClock=%s, format=%s, channel=%s",
               static_cast<int>(this->config().pins.bitClock),
               static_cast<int>(this->config().pins.wordSelect),
               static_cast<int>(this->config().pins.dataIn),
               _hostClockRoleName(_linkConfig.hostClockRole), _formatName(_linkConfig.format),
               _channelName(_linkConfig.channel));
    }

    static constexpr const char *
    _hostClockRoleName(I2SHostClockRole hostClockRole) {
        switch (hostClockRole) {
        case I2SHostClockRole::ConsumesExternalClock:
            return "host-consumes-external-bclk-ws";
        case I2SHostClockRole::ProvidesClock:
            return "host-provides-bclk-ws";
        default:
            return "unknown";
        }
    }

    static constexpr const char *_formatName(I2SFormat format) {
        switch (format) {
        case I2SFormat::Standard:
            return "standard";
        case I2SFormat::Lsb:
            return "lsb";
        case I2SFormat::Msb:
            return "msb";
        case I2SFormat::Philips:
            return "philips";
        case I2SFormat::RightJustified:
            return "right-justified";
        case I2SFormat::LeftJustified:
            return "left-justified";
        case I2SFormat::Pcm:
            return "pcm";
        default:
            return "unknown";
        }
    }

    static constexpr const char *_channelName(I2SChannelSelect channel) {
        switch (channel) {
        case I2SChannelSelect::Stereo:
            return "stereo";
        case I2SChannelSelect::Left:
            return "left";
        case I2SChannelSelect::Right:
            return "right";
        default:
            return "unknown";
        }
    }

    void _resetReadiness() {
        _ready.store(false, std::memory_order_release);
        _lastProbeMs = 0;
        _lastWaitingLogMs = 0;
        _consecutiveEmptyReads = 0;
        _probes.store(0, std::memory_order_release);
        _emptyReads.store(0, std::memory_order_release);
        _readTimeouts.store(0, std::memory_order_release);
        _readErrors.store(0, std::memory_order_release);
        _observedBytes.store(0, std::memory_order_release);
        _lastDataMs.store(0, std::memory_order_release);
        _lastReadStatus.store(0, std::memory_order_release);
        _lastDataAvailableLogMs = 0;
    }

    [[nodiscard]] bool _shouldLogDataAvailable(uint32_t nowMs) {
        if (_lastDataAvailableLogMs != 0 &&
            nowMs - _lastDataAvailableLogMs <
                this->config().readiness.waitingLogIntervalMs) {
            return false;
        }
        _lastDataAvailableLogMs = nowMs;
        return true;
    }

    Platform::I2SInputStream _input;
    I2SLinkConfig _linkConfig{};
    AudioInfo _audioInfo{};
    std::array<uint8_t, i2sMaxProbeBytes> _probeBuffer{};
    uint32_t _lastProbeMs = 0;
    uint32_t _lastWaitingLogMs = 0;
    uint32_t _lastDataAvailableLogMs = 0;
    uint8_t _consecutiveEmptyReads = 0;
    std::atomic<bool> _ready{false};
    std::atomic<uint32_t> _probes{0};
    std::atomic<uint32_t> _emptyReads{0};
    std::atomic<uint32_t> _readTimeouts{0};
    std::atomic<uint32_t> _readErrors{0};
    std::atomic<uint32_t> _observedBytes{0};
    std::atomic<uint32_t> _lastDataMs{0};
    std::atomic<int32_t> _lastReadStatus{0};
};

} // namespace Totem::Audio::detail
