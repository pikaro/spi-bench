#pragma once

#include "Audio/Interfaces/SourceConfig.hpp"
#include "Audio/Interfaces/Types.hpp"
#include "Audio/detail/PlatformSelect.hpp"
#include "Audio/detail/Sources/A2DPSource.hpp"
#include "Audio/detail/Sources/BtstackA2DPSource.hpp"
#include "Audio/detail/Sources/I2SSource.hpp"
#include "Audio/detail/Sources/IAudioSource.hpp"
#include "Audio/detail/Sources/WavSource.hpp"
#include "Audio/detail/Types.hpp"
#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <cstdint>

namespace Totem::Audio::detail {

class AudioSource : public HasLifecycle<AudioSource, AudioSourceConfig>,
                    public IAudioSource {
    friend class HasLifecycle<AudioSource, AudioSourceConfig>;
    friend struct LifecycleContract<AudioSource, AudioSourceConfig>;

  public:
    DELETE_COPY(AudioSource)
    DELETE_MOVE(AudioSource)

    static constexpr const char *name = "Audio::Source";
    static constexpr LogComponent logComponent =
        Totem::Audio::detail::logComponent;

    AudioSource() = default;

    [[nodiscard]] bool active() const override {
        return HasLifecycle<AudioSource, AudioSourceConfig>::active();
    }

    [[nodiscard]] const AudioInfo &audioInfo() const override {
        return _active == nullptr ? _emptyAudioInfo : _active->audioInfo();
    }

    [[nodiscard]] bool ready() const override {
        return _active != nullptr && _active->ready();
    }

    [[nodiscard]] const char *sourceName() const override {
        return _active == nullptr ? "none" : _active->sourceName();
    }

    bool pollReadiness(uint32_t nowMs) override {
        return _active != nullptr && _active->pollReadiness(nowMs);
    }

    void observeReadResult(std::size_t bytesRead, uint32_t nowMs) override {
        if (_active == nullptr) {
            return;
        }
        _active->observeReadResult(bytesRead, nowMs);
    }

    Platform::AudioStream &stream() override {
        ABORT_IF_NULL(_active, "Audio source is not active");
        return _active->stream();
    }

  private:
    ReturnCode _onBegin() {
        IAudioSource *selected = nullptr;
        auto ret = OK();
        switch (config().kind) {
        case AudioSourceKind::I2S:
            selected = &_i2s;
            ret = _i2s.begin(*config().i2s);
            break;
        case AudioSourceKind::WavFile:
            selected = &_wav;
            ret = _wav.begin(*config().wav);
            break;
        case AudioSourceKind::A2DP:
            selected = &_a2dp;
            ret = _a2dp.begin(*config().a2dp);
            break;
        case AudioSourceKind::BtstackA2DP:
            selected = &_btstackA2DP;
            ret = _btstackA2DP.begin(*config().btstackA2DP);
            break;
        default:
            return ERR(CoreError, InvalidArgument);
        }
        FAIL_IF_ERR_FWD(ret, "Failed to begin selected audio source");
        _active = selected;
        _log_i("Selected audio source: %s", _active->sourceName());
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        switch (config().kind) {
        case AudioSourceKind::I2S:
            ret = _i2s.end();
            break;
        case AudioSourceKind::WavFile:
            ret = _wav.end();
            break;
        case AudioSourceKind::A2DP:
            ret = _a2dp.end();
            break;
        case AudioSourceKind::BtstackA2DP:
            ret = _btstackA2DP.end();
            break;
        default:
            break;
        }
        _active = nullptr;
        return ret;
    }

    I2SSource _i2s{};
    WavSource _wav{};
    A2DPSource _a2dp{};
    BtstackA2DPSource _btstackA2DP{};
    IAudioSource *_active = nullptr;
    AudioInfo _emptyAudioInfo{};
};

} // namespace Totem::Audio::detail
