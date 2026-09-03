#pragma once

#include "LedDisplay/Interfaces/Color.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Outputs/detail/Sk9822Encoder.hpp"
#include "LedDisplay/Outputs/detail/platform/Sk9822SpiESP32.hpp"
#include "LedDisplay/detail/Metrics.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Totem::LedDisplay::Outputs {

class Sk9822SpiOutput {
  public:
    DELETE_COPY(Sk9822SpiOutput)
    DELETE_MOVE(Sk9822SpiOutput)

    Sk9822SpiOutput() = default;

    ReturnCode begin(const Config &config) {
        FAIL_IF(_configured, ERR(CoreError, InvalidState),
                "SK9822 output is already configured");
        FAIL_IF(!config.sk9822.validate(), ERR(CoreError, InvalidArgument),
                "Invalid SK9822 output configuration");

        _brightness = config.globalBrightness;
        _configuredOutputValueFloor = config.outputValueFloor;
        _outputLumaFloor = config.outputLumaFloor;
        _colorOrder = config.sk9822.colorOrder;

        FAIL_IF(detail::Sk9822Encoder::encodeBlack(Config::ownedPixelCount,
                                                   _encodedFrame) !=
                    detail::Sk9822EncodeResult::Ok,
                ERR(CoreError, InvalidSize),
                "Failed to prepare initial SK9822 black frame");
        FAIL_IF_ERR_FWD(_transport.begin(config.sk9822, _encodedFrame),
                        "Failed to initialize SK9822 SPI3 transport");
        auto ret = _transport.transmit(_encodedFrame);
        if (!ret.ok()) {
            (void)_transport.deinit();
            FAIL_ERR_FWD(ret, "Failed to transmit initial SK9822 black frame");
        }
        _configured = true;
        _log_i("SK9822 SPI output ready: pixels=%zu bytes=%zu clock=%luHz "
               "brightness=%u level=%u valueFloor=%u lumaFloor=%u",
               Config::ownedPixelCount, _encodedFrame.size(),
               static_cast<unsigned long>(config.sk9822.clockHz),
               static_cast<unsigned>(_brightness),
               static_cast<unsigned>(
                   detail::Sk9822Encoder::brightnessLevel(_brightness)),
               static_cast<unsigned>(_configuredOutputValueFloor),
               static_cast<unsigned>(_outputLumaFloor));
        return OK();
    }

    ReturnCode setBrightness(uint8_t brightness) {
        FAIL_IF(!_configured, ERR(CoreError, InvalidState),
                "SK9822 output is not configured");
        _brightness = brightness;
        _log_i("SK9822 brightness set to %u hardwareLevel=%u",
               static_cast<unsigned>(_brightness),
               static_cast<unsigned>(
                   detail::Sk9822Encoder::brightnessLevel(_brightness)));
        return OK();
    }

    ReturnCode show(std::span<const HsvColor> frame) {
        FAIL_IF(!_configured, ERR(CoreError, InvalidState),
                "SK9822 output is not configured");
        FAIL_IF(frame.size() != Config::ownedPixelCount,
                ERR(CoreError, InvalidSize), "Unexpected SK9822 frame size");

        // A previous bounded wait may have timed out while DMA still owned the
        // frame. Reap it before the encoder mutates that storage.
        FAIL_IF_ERR_FWD(_waitForTransfer(),
                        "Failed to reap previous SK9822 transfer");

        const auto encodeStartUs = ::platform::get_time_us();
        FAIL_IF(detail::Sk9822Encoder::encode(
                    frame, _brightness, _configuredOutputValueFloor,
                    _outputLumaFloor, _colorOrder,
                    _encodedFrame) != detail::Sk9822EncodeResult::Ok,
                ERR(CoreError, InvalidSize), "Failed to encode SK9822 frame");
        LedDisplay::detail::metrics().recordEncodeDuration(
            static_cast<uint32_t>(::platform::get_time_us() - encodeStartUs));

        const auto queueStartUs = ::platform::get_time_us();
        auto ret = _transport.queue(_encodedFrame);
        LedDisplay::detail::metrics().recordSpiQueueDuration(
            static_cast<uint32_t>(::platform::get_time_us() - queueStartUs));
        if (!ret.ok()) {
            return ret;
        }
        return _waitForTransfer();
    }

    ReturnCode deinit() {
        if (!_configured) {
            return OK();
        }
        auto ret = _waitForTransfer();
        if (!ret.ok()) {
            return ret;
        }
        if (detail::Sk9822Encoder::encodeBlack(Config::ownedPixelCount,
                                               _encodedFrame) ==
            detail::Sk9822EncodeResult::Ok) {
            ret = _transport.transmit(_encodedFrame);
        } else {
            ret = ERR(CoreError, InvalidSize);
        }
        const auto transportRet = _transport.deinit();
        ret.combine(transportRet);
        if (transportRet.ok()) {
            _configured = false;
        }
        return ret;
    }

  private:
    static constexpr size_t encodedFrameSize =
        detail::Sk9822Encoder::encodedSize(Config::ownedPixelCount);

    ReturnCode _waitForTransfer() {
        const auto waitStartUs = ::platform::get_time_us();
        auto ret = _transport.wait();
        LedDisplay::detail::metrics().recordSpiWaitDuration(
            static_cast<uint32_t>(::platform::get_time_us() - waitStartUs));
        return ret;
    }

    alignas(4) std::array<std::byte, encodedFrameSize> _encodedFrame{};
    detail::platform::Sk9822SpiESP32 _transport{};
    Sk9822WireColorOrder _colorOrder = Sk9822WireColorOrder::Bgr;
    uint8_t _brightness = 255;
    uint8_t _configuredOutputValueFloor = 0;
    uint8_t _outputLumaFloor = 0;
    bool _configured = false;
};

} // namespace Totem::LedDisplay::Outputs
