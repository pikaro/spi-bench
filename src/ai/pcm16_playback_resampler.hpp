#pragma once

#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include "dsps_fir.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace AiAudio {

// The MAX98357 explicitly does not support 24 kHz LRCLK. Convert PocketTTS's
// native 24 kHz PCM to the nearest higher supported rate so no source bandwidth
// is discarded. ESP-DSP keeps the rational FIR state across WebSocket chunks.
class Pcm16PlaybackResampler {
  public:
    static constexpr uint32_t inputSampleRate = 24000;
    static constexpr uint32_t outputSampleRate = 32000;
    static constexpr std::size_t maximumInputSamples = 512;

    ReturnCode reset() {
        const auto result = dsps_firmr_init_f32(
            &_filter, const_cast<float *>(coefficients.data()), _delay.data(),
            static_cast<int>(coefficients.size()), interpolation, decimation,
            0);
        FAIL_IF(result != ESP_OK, ERR(CoreError, OperationFailed),
                "Failed to initialize 24-to-32 kHz playback resampler");
        return OK();
    }

    ReturnCode process(std::span<const int16_t> input,
                       std::span<int16_t> &output) {
        FAIL_IF(input.empty() || input.size() > maximumInputSamples,
                ERR(CoreError, InvalidArgument),
                "Invalid playback resampler input size");

        std::transform(
            input.begin(), input.end(), _input.begin(),
            [](int16_t sample) { return static_cast<float>(sample); });
        const auto outputSamples =
            dsps_firmr_f32(&_filter, _input.data(), _output.data(),
                           static_cast<int>(input.size()));
        FAIL_IF(outputSamples <= 0 ||
                    static_cast<std::size_t>(outputSamples) > _pcm.size(),
                ERR(CoreError, InvalidData),
                "Playback resampler produced an invalid sample count");

        for (int32_t index = 0; index < outputSamples; ++index) {
            const auto rounded = std::lround(_output[index]);
            _pcm[index] = static_cast<int16_t>(
                std::clamp<long>(rounded, std::numeric_limits<int16_t>::min(),
                                 std::numeric_limits<int16_t>::max()));
        }
        output = std::span<int16_t>{_pcm.data(),
                                    static_cast<std::size_t>(outputSamples)};
        return OK();
    }

  private:
    static constexpr int interpolation = 4;
    static constexpr int decimation = 3;
    static constexpr std::size_t maximumOutputSamples =
        (maximumInputSamples * interpolation + decimation - 1U) / decimation;

    // 128-tap Kaiser-windowed low-pass, 12 kHz cutoff at the 96 kHz
    // interpolated rate, normalized for unity gain across all four phases.
    alignas(16) static inline constexpr std::array<float, 128> coefficients{
        -2.8614971859e-05F, -1.0458868240e-04F, -1.4846243791e-04F,
        -8.3613529949e-05F, 1.1019420624e-04F,  3.4231867483e-04F,
        4.3215795355e-04F,  2.2246856171e-04F,  -2.7285974599e-04F,
        -7.9886299528e-04F, -9.5929527414e-04F, -4.7302797106e-04F,
        5.5877843272e-04F,  1.5825290450e-03F,  1.8448355741e-03F,
        8.8572866297e-04F,  -1.0212715922e-03F, -2.8291829837e-03F,
        -3.2319761587e-03F, -1.5230136199e-03F, 1.7260183950e-03F,
        4.7055085350e-03F,  5.2958886954e-03F,  2.4611406440e-03F,
        -2.7531978696e-03F, -7.4151907615e-03F, -8.2511609222e-03F,
        -3.7939060029e-03F, 4.2019959811e-03F,  1.1212096215e-02F,
        1.2367761664e-02F,  5.6406515982e-03F,  -6.2002963626e-03F,
        -1.6428576812e-02F, -1.8005278491e-02F, -8.1634237651e-03F,
        8.9254551425e-03F,  2.3536265793e-02F,  2.5686740175e-02F,
        1.1604190025e-02F,  -1.2649731976e-02F, -3.3280454316e-02F,
        -3.6263968783e-02F, -1.6369549841e-02F, 1.7845682620e-02F,
        4.6998802376e-02F,  5.1319879850e-02F,  2.3242916467e-02F,
        -2.5458997527e-02F, -6.7477751908e-02F, -7.4295946054e-02F,
        -3.4008066551e-02F, 3.7754503619e-02F,  1.0177480596e-01F,
        1.1447535748e-01F,  5.3835809652e-02F,  -6.1871157911e-02F,
        -1.7446348434e-01F, -2.0834592282e-01F, -1.0639704029e-01F,
        1.3774851931e-01F,  4.6800289397e-01F,  7.8271143495e-01F,
        9.7431454303e-01F,  9.7431454303e-01F,  7.8271143495e-01F,
        4.6800289397e-01F,  1.3774851931e-01F,  -1.0639704029e-01F,
        -2.0834592282e-01F, -1.7446348434e-01F, -6.1871157911e-02F,
        5.3835809652e-02F,  1.1447535748e-01F,  1.0177480596e-01F,
        3.7754503619e-02F,  -3.4008066551e-02F, -7.4295946054e-02F,
        -6.7477751908e-02F, -2.5458997527e-02F, 2.3242916467e-02F,
        5.1319879850e-02F,  4.6998802376e-02F,  1.7845682620e-02F,
        -1.6369549841e-02F, -3.6263968783e-02F, -3.3280454316e-02F,
        -1.2649731976e-02F, 1.1604190025e-02F,  2.5686740175e-02F,
        2.3536265793e-02F,  8.9254551425e-03F,  -8.1634237651e-03F,
        -1.8005278491e-02F, -1.6428576812e-02F, -6.2002963626e-03F,
        5.6406515982e-03F,  1.2367761664e-02F,  1.1212096215e-02F,
        4.2019959811e-03F,  -3.7939060029e-03F, -8.2511609222e-03F,
        -7.4151907615e-03F, -2.7531978696e-03F, 2.4611406440e-03F,
        5.2958886954e-03F,  4.7055085350e-03F,  1.7260183950e-03F,
        -1.5230136199e-03F, -3.2319761587e-03F, -2.8291829837e-03F,
        -1.0212715922e-03F, 8.8572866297e-04F,  1.8448355741e-03F,
        1.5825290450e-03F,  5.5877843272e-04F,  -4.7302797106e-04F,
        -9.5929527414e-04F, -7.9886299528e-04F, -2.7285974599e-04F,
        2.2246856171e-04F,  4.3215795355e-04F,  3.4231867483e-04F,
        1.1019420624e-04F,  -8.3613529949e-05F, -1.4846243791e-04F,
        -1.0458868240e-04F, -2.8614971859e-05F,
    };

    fir_f32_t _filter{};
    alignas(16) std::array<float, coefficients.size() / interpolation> _delay{};
    alignas(16) std::array<float, maximumInputSamples> _input{};
    alignas(16) std::array<float, maximumOutputSamples> _output{};
    alignas(16) std::array<int16_t, maximumOutputSamples> _pcm{};
};

} // namespace AiAudio
