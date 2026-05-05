#pragma once

#include "Audio/detail/FftAnalyzer.hpp"
#include "Audio/detail/FftDisplay.hpp"
#include "Audio/detail/Sources/A2DPSource.hpp"
#include "Audio/detail/Sources/AudioSource.hpp"
#include "Audio/detail/Sources/BtstackA2DPSource.hpp"
#include "Audio/detail/Sources/I2SSource.hpp"
#include "Audio/detail/Sources/WavSource.hpp"

namespace Totem::Audio {

using detail::A2DPSource;
using detail::AudioSource;
using detail::BtstackA2DPSource;
using detail::FftAnalyzer;
using detail::FftDisplay;
using detail::I2SSource;
using detail::WavSource;

} // namespace Totem::Audio
