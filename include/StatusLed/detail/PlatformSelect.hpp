#pragma once

#include "Macros/Facade.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "StatusLed/Interfaces/Types.hpp"
#include "StatusLed/detail/platform/RmtWs2812.hpp"
#include "StatusLed/detail/platform/SplitRgbGpio.hpp"
#include "Types/Error.hpp"

namespace Totem::StatusLed::detail {

class Platform {
  public:
    DELETE_COPY(Platform)
    DELETE_MOVE(Platform)

    Platform() = default;

    ReturnCode begin(const Config &config) {
        if (_active) {
            return ERR(LifecycleError, Active);
        }

        switch (config.backend) {
        case OutputBackend::RmtWs2812:
            FAIL_IF_ERR_FWD(_rmt.begin(config.pin, config.colorOrder),
                            "Failed to begin status LED RMT output");
            break;
        case OutputBackend::SplitRgbGpio:
            FAIL_IF_ERR_FWD(_splitRgb.begin(config.splitRgbGpio),
                            "Failed to begin status LED split RGB output");
            break;
        default:
            FAIL_ERR(NotSupported, "Unsupported status LED backend");
        }

        _backend = config.backend;
        _active = true;
        return OK();
    }

    ReturnCode show(RgbColor color) {
        if (!_active) {
            return OK();
        }

        switch (_backend) {
        case OutputBackend::RmtWs2812:
            return _rmt.show(color);
        case OutputBackend::SplitRgbGpio:
            return _splitRgb.show(color);
        default:
            return ERR(NotSupported);
        }
    }

    ReturnCode deinit() {
        if (!_active) {
            return OK();
        }

        auto ret = OK();
        switch (_backend) {
        case OutputBackend::RmtWs2812:
            ret = _rmt.deinit();
            break;
        case OutputBackend::SplitRgbGpio:
            ret = _splitRgb.deinit();
            break;
        default:
            ret = ERR(NotSupported);
            break;
        }

        _active = false;
        return ret;
    }

  private:
    platform::RmtWs2812 _rmt{};
    platform::SplitRgbGpio _splitRgb{};
    OutputBackend _backend = OutputBackend::RmtWs2812;
    bool _active = false;
};

} // namespace Totem::StatusLed::detail
