#pragma once

#include "Macros/Facade.hpp"
#include "Platform/Hardware.hpp"
#include "Platform/PlatformSelect.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "StatusLed/Interfaces/Types.hpp"
#include "StatusLed/detail/Types.hpp"
#include "Types/Error.hpp"
#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_err.h"
#include "soc/gpio_num.h"
#include "soc/soc_caps.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace Totem::StatusLed::detail::platform {

class RmtWs2812 {
  public:
    DELETE_COPY(RmtWs2812)
    DELETE_MOVE(RmtWs2812)

    RmtWs2812() = default;

    ReturnCode begin(Pin pin, ColorOrder colorOrder) {
        if (_active) {
            return ERR(LifecycleError, Active);
        }

        FAIL_IF_NOT(isValidPin(pin), ERR(InvalidArgument),
                    "Invalid status LED RMT pin %u",
                    static_cast<unsigned>(pin));

        rmt_tx_channel_config_t channelConfig = {};
        channelConfig.gpio_num = static_cast<gpio_num_t>(pin);
        channelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
        channelConfig.resolution_hz = resolutionHz;
        channelConfig.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
        channelConfig.trans_queue_depth = 1;

        FAIL_IF_PLATFORM_FWD(
            rmt_new_tx_channel(&channelConfig, &_channel),
            "Failed to create status LED RMT channel");

        rmt_copy_encoder_config_t encoderConfig = {};
        auto encoderRet = rmt_new_copy_encoder(&encoderConfig, &_encoder);
        if (encoderRet != ESP_OK) {
            (void)rmt_del_channel(_channel);
            _channel = nullptr;
            FAIL_IF_PLATFORM_FWD(encoderRet,
                                 "Failed to create status LED RMT encoder");
        }

        auto enableRet = rmt_enable(_channel);
        if (enableRet != ESP_OK) {
            (void)rmt_del_encoder(_encoder);
            _encoder = nullptr;
            (void)rmt_del_channel(_channel);
            _channel = nullptr;
            FAIL_IF_PLATFORM_FWD(enableRet,
                                 "Failed to enable status LED RMT channel");
        }

        _colorOrder = colorOrder;
        _active = true;
        return OK();
    }

    ReturnCode show(RgbColor color) {
        if (!_active) {
            return OK();
        }
        if (_hasLastColor && _lastColor == color) {
            return OK();
        }

        auto symbols = encode(color, _colorOrder);
        rmt_transmit_config_t transmitConfig = {};
        transmitConfig.loop_count = 0;
        FAIL_IF_PLATFORM_FWD(
            rmt_transmit(_channel, _encoder, symbols.data(),
                         symbols.size() * sizeof(rmt_symbol_word_t),
                         &transmitConfig),
            "Failed to transmit status LED color");
        FAIL_IF_PLATFORM_FWD(
            rmt_tx_wait_all_done(_channel, transmitTimeoutMs),
            "Timed out waiting for status LED color transmit");

        _lastColor = color;
        _hasLastColor = true;
        return OK();
    }

    ReturnCode deinit() {
        if (!_active) {
            return OK();
        }

        auto ret = OK();
        if (_channel != nullptr) {
            auto waitRet = ::platform::map_platform_error(
                rmt_tx_wait_all_done(_channel, transmitTimeoutMs));
            ret.combine(waitRet);
            auto disableRet =
                ::platform::map_platform_error(rmt_disable(_channel));
            ret.combine(disableRet);
        }
        if (_encoder != nullptr) {
            auto encoderRet =
                ::platform::map_platform_error(rmt_del_encoder(_encoder));
            ret.combine(encoderRet);
        }
        if (_channel != nullptr) {
            auto channelRet =
                ::platform::map_platform_error(rmt_del_channel(_channel));
            ret.combine(channelRet);
        }

        _encoder = nullptr;
        _channel = nullptr;
        _active = false;
        _hasLastColor = false;
        return ret;
    }

    [[nodiscard]] static bool isValidPin(Pin pin) {
        return GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(pin));
    }

  private:
    static constexpr uint32_t resolutionHz = 10'000'000;
    static constexpr int transmitTimeoutMs = 10;
    static constexpr std::size_t dataSymbolCount = 24;
    static constexpr std::size_t resetSymbolCount = 1;
    static constexpr std::size_t symbolCount =
        dataSymbolCount + resetSymbolCount;

    using SymbolBuffer = std::array<rmt_symbol_word_t, symbolCount>;

    static rmt_symbol_word_t symbol(uint16_t highTicks, uint16_t lowTicks) {
        rmt_symbol_word_t out{};
        out.level0 = 1;
        out.duration0 = highTicks;
        out.level1 = 0;
        out.duration1 = lowTicks;
        return out;
    }

    static rmt_symbol_word_t resetSymbol() {
        rmt_symbol_word_t out{};
        out.level0 = 0;
        out.duration0 = 250;
        out.level1 = 0;
        out.duration1 = 250;
        return out;
    }

    static SymbolBuffer encode(RgbColor color, ColorOrder colorOrder) {
        const auto ordered = orderedBytes(color, colorOrder);
        SymbolBuffer symbols{};
        std::size_t index = 0;
        for (const auto value : ordered) {
            for (int bit = 7; bit >= 0; --bit) {
                symbols[index++] = ((value >> bit) & 0x01U) != 0U
                                       ? symbol(9, 3)
                                       : symbol(3, 9);
            }
        }
        symbols[index] = resetSymbol();
        return symbols;
    }

    static std::array<uint8_t, 3> orderedBytes(RgbColor color,
                                               ColorOrder colorOrder) {
        switch (colorOrder) {
        case ColorOrder::RGB:
            return {color.red, color.green, color.blue};
        case ColorOrder::GRB:
        default:
            return {color.green, color.red, color.blue};
        }
    }

    rmt_channel_handle_t _channel = nullptr;
    rmt_encoder_handle_t _encoder = nullptr;
    ColorOrder _colorOrder = ColorOrder::GRB;
    RgbColor _lastColor{};
    bool _hasLastColor = false;
    bool _active = false;
};

} // namespace Totem::StatusLed::detail::platform
