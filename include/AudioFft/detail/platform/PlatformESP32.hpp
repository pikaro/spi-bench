// IWYU pragma: private

#pragma once

#include "AudioTools/AudioLibs/AudioFFT.h"
#include "AudioTools/AudioLibs/AudioEspressifFFT.h"
#include "AudioTools/AudioLibs/AudioRealFFT.h"
#include "AudioTools/AudioLibs/FFT/FFTWindows.h"
#include "AudioTools/CoreAudio/AudioStreams.h"
#include "AudioTools/CoreAudio/BaseStream.h"
#include "AudioTools/CoreAudio/StreamCopy.h"

namespace Totem::AudioFft::detail::platform {

struct Platform {
    using RealFftSink = audio_tools::AudioRealFFT;
    using EspressifFftSink = audio_tools::AudioEspressifFFT;
    using StreamCopier = audio_tools::StreamCopy;
    using AudioFftBase = audio_tools::AudioFFTBase;
    using AudioFftConfig = audio_tools::AudioFFTConfig;
    using WindowFunction = audio_tools::WindowFunction;
    using HammingWindow = audio_tools::Hamming;
    using HannWindow = audio_tools::Hann;
};

} // namespace Totem::AudioFft::detail::platform
