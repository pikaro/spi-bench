#pragma once

#include "Audio/Interfaces/SourceConfig.hpp"
#include "Audio/Interfaces/Types.hpp"
#include "Audio/detail/PlatformSelect.hpp"
#include "Audio/detail/Types.hpp"
#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Totem::Audio::detail {

class FftAnalyzer;

class I2SSource : public HasLifecycle<I2SSource, I2SSourceConfig> {
    friend class HasLifecycle<I2SSource, I2SSourceConfig>;
    friend struct LifecycleContract<I2SSource, I2SSourceConfig>;
    friend class FftAnalyzer;

  public:
    DELETE_COPY(I2SSource)
    DELETE_MOVE(I2SSource)

    static constexpr const char *name = "Audio::I2SSource";
    static constexpr LogComponent logComponent =
        Totem::Audio::detail::logComponent;

    I2SSource() = default;

    [[nodiscard]] const AudioInfo &audioInfo() const { return _audioInfo; }
    [[nodiscard]] const I2SDeviceConfig &deviceConfig() const {
        return _deviceConfig;
    }
    [[nodiscard]] bool ready() const {
        return _ready.load(std::memory_order_acquire);
    }
    [[nodiscard]] I2SSourceStatus status() const {
        return I2SSourceStatus{
            .ready = ready(),
            .probes = _probes.load(std::memory_order_acquire),
            .emptyReads = _emptyReads.load(std::memory_order_acquire),
            .observedBytes = _observedBytes.load(std::memory_order_acquire),
            .lastDataMs = _lastDataMs.load(std::memory_order_acquire),
        };
    }

  private:
    ReturnCode _onBegin() {
        _deviceConfig = this->config().resolvedDevice();
        const auto &pins = this->config().pins;
        _log_i("Starting I2S input: %lu Hz, %u ch, %u bit, pins bck=%d ws=%d "
               "data=%d",
               static_cast<unsigned long>(_deviceConfig.audio.sampleRate),
               _deviceConfig.audio.channels, _deviceConfig.audio.bitsPerSample,
               pins.bitClock, pins.wordSelect, pins.dataIn);
        FAIL_IF_ERR_FWD(_input.begin(pins, _deviceConfig),
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

    Platform::I2SInputStream &input() { return _input; }

    bool pollReadiness(uint32_t nowMs) {
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

    void observeReadResult(size_t bytesRead, uint32_t nowMs) {
        if (bytesRead > 0) {
            _markDataAvailable(bytesRead, nowMs);
            return;
        }

        _emptyReads.fetch_add(1, std::memory_order_acq_rel);
        if (!ready()) {
            return;
        }

        ++_consecutiveEmptyReads;
        if (_consecutiveEmptyReads >=
            this->config().readiness.emptyReadsBeforeOffline) {
            _consecutiveEmptyReads = 0;
            _ready.store(false, std::memory_order_release);
            _log_w("I2S source stopped producing data; entering probe mode");
        }
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void _markDataAvailable(size_t bytesRead, uint32_t nowMs) {
        _consecutiveEmptyReads = 0;
        _observedBytes.fetch_add(static_cast<uint32_t>(bytesRead),
                                 std::memory_order_acq_rel);
        _lastDataMs.store(nowMs, std::memory_order_release);
        const auto wasReady = _ready.exchange(true, std::memory_order_acq_rel);
        if (!wasReady) {
            _log_i("I2S source is producing data");
        }
    }

    void _resetReadiness() {
        _ready.store(false, std::memory_order_release);
        _lastProbeMs = 0;
        _consecutiveEmptyReads = 0;
        _probes.store(0, std::memory_order_release);
        _emptyReads.store(0, std::memory_order_release);
        _observedBytes.store(0, std::memory_order_release);
        _lastDataMs.store(0, std::memory_order_release);
    }

    Platform::I2SInputStream _input;
    I2SDeviceConfig _deviceConfig{};
    AudioInfo _audioInfo{};
    std::array<uint8_t, i2sMaxProbeBytes> _probeBuffer{};
    uint32_t _lastProbeMs = 0;
    uint8_t _consecutiveEmptyReads = 0;
    std::atomic<bool> _ready{false};
    std::atomic<uint32_t> _probes{0};
    std::atomic<uint32_t> _emptyReads{0};
    std::atomic<uint32_t> _observedBytes{0};
    std::atomic<uint32_t> _lastDataMs{0};
};

} // namespace Totem::Audio::detail
