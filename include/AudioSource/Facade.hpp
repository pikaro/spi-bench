#pragma once

#include "AudioSource/detail/Sources/I2SSource.hpp"
#include "AudioSource/detail/Sources/IAudioSource.hpp"
#include "AudioSource/detail/Sources/PcmRingStream.hpp"
#include "AudioSource/detail/Sources/WavSource.hpp"

namespace Totem::AudioSource {

using detail::I2SSource;
using detail::IAudioSource;
using detail::PcmRingStream;
using detail::WavSource;

} // namespace Totem::AudioSource
