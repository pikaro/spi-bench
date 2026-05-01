#pragma once

#include "Audio/Interfaces/Types.hpp"
#include "Audio/detail/PlatformSelect.hpp"
#include "Audio/detail/Types.hpp"
#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"

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

  private:
    ReturnCode _onBegin() {
        _deviceConfig = this->config().resolvedDevice();
        _log_i("Starting I2S input: %lu Hz, %u ch, %u bit, pins bck=%d ws=%d "
               "data=%d",
               static_cast<unsigned long>(_deviceConfig.audio.sampleRate),
               _deviceConfig.audio.channels,
               _deviceConfig.audio.bitsPerSample,
               _deviceConfig.pins.bitClock,
               _deviceConfig.pins.wordSelect,
               _deviceConfig.pins.dataIn);
        FAIL_IF_ERR_FWD(_input.begin(_deviceConfig),
                        "Failed to start I2S input");
        _audioInfo = _input.audioInfo();
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = _input.end();
        _audioInfo = {};
        return ret;
    }

    Platform::I2SInputStream &input() { return _input; }

    Platform::I2SInputStream _input{};
    I2SDeviceConfig _deviceConfig{};
    AudioInfo _audioInfo{};
};

} // namespace Totem::Audio::detail
