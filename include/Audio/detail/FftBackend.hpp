#pragma once

#include "Audio/Interfaces/AnalyzerConfig.hpp"
#include "Audio/Interfaces/Types.hpp"
#include "Audio/detail/PlatformSelect.hpp"
#include "AudioTools/CoreAudio/AudioTypes.h"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <cstdint>

namespace Totem::Audio::detail {

class FftBackend {
  public:
    using Callback = void (*)(Platform::AudioFftBase &fft);

    DELETE_COPY(FftBackend)
    DELETE_MOVE(FftBackend)

    FftBackend() = default;

    ReturnCode begin(const FftAnalyzerConfig &config, AudioInfo audio,
                     Platform::WindowFunction *windowFunction,
                     Callback callback, void *callbackRef) {
        FAIL_IF(_active != nullptr, ERR(CoreError, InvalidState),
                "FFT backend is already active");
        auto *selected = _select(config.backend);
        FAIL_IF_NULL(selected, ERR(CoreError, InvalidArgument),
                     "Unsupported FFT backend");

        auto fftConfig = selected->defaultConfig(audio_tools::TX_MODE);
        fftConfig.sample_rate = audio.sampleRate;
        fftConfig.channels = audio.channels;
        fftConfig.bits_per_sample = audio.bitsPerSample;
        fftConfig.channel_used = config.channel;
        fftConfig.length = config.length;
        fftConfig.stride = config.stride;
        fftConfig.callback = callback;
        fftConfig.ref = callbackRef;
        fftConfig.window_function_fft = windowFunction;

        FAIL_IF(!selected->begin(fftConfig), ERR(CoreError, OperationFailed),
                "Failed to begin %s FFT backend", backendName(config.backend));
        _active = selected;
        _library = config.backend;
        return OK();
    }

    ReturnCode end() {
        if (_active == nullptr) {
            return OK();
        }
        _active->end();
        _active = nullptr;
        return OK();
    }

    [[nodiscard]] bool active() const { return _active != nullptr; }
    [[nodiscard]] FftBackendLibrary library() const { return _library; }

    [[nodiscard]] Platform::AudioFftBase &fft() {
        ABORT_IF_NULL(_active, "FFT backend is not active");
        return *_active;
    }

    [[nodiscard]] audio_tools::AudioStream &stream() { return fft(); }

    [[nodiscard]] static constexpr const char *
    backendName(FftBackendLibrary library) {
        switch (library) {
        case FftBackendLibrary::RealFft:
            return "real-fft";
        case FftBackendLibrary::EspressifFft:
            return "espressif-fft";
        default:
            return "unknown";
        }
    }

  private:
    Platform::AudioFftBase *_select(FftBackendLibrary library) {
        switch (library) {
        case FftBackendLibrary::RealFft:
            return &_real;
        case FftBackendLibrary::EspressifFft:
            return &_espressif;
        default:
            return nullptr;
        }
    }

    Platform::RealFftSink _real;
    Platform::EspressifFftSink _espressif;
    Platform::AudioFftBase *_active = nullptr;
    FftBackendLibrary _library = FftBackendLibrary::RealFft;
};

} // namespace Totem::Audio::detail
