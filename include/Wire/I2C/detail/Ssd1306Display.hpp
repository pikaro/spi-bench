#pragma once

#include "Base/HasLifecycle.hpp"
#include "Wire/I2C/Interfaces/DisplayConfig.hpp"
#include "Wire/I2C/Interfaces/Types.hpp"
#include "Wire/I2C/detail/Device.hpp"
#include "Wire/I2C/detail/Master.hpp"
#include "Wire/I2C/detail/Types.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Totem::Wire::I2C::detail {

class Ssd1306Display : public HasLifecycle<Ssd1306Display, Ssd1306Config> {
    friend class HasLifecycle<Ssd1306Display, Ssd1306Config>;
    friend struct LifecycleContract<Ssd1306Display, Ssd1306Config>;

  public:
    DELETE_COPY(Ssd1306Display)
    DELETE_MOVE(Ssd1306Display)

    static constexpr const char *name = "I2C::Ssd1306Display";
    static constexpr LogComponent logComponent =
        Totem::Wire::I2C::detail::logComponent;

    explicit Ssd1306Display(Master &master) : _master(master) {}

    [[nodiscard]] uint8_t width() const { return config().width; }
    [[nodiscard]] uint8_t height() const { return config().height; }

    void clear() { _framebuffer.fill(0x00); }
    void fill() { _framebuffer.fill(0xFF); }

    void setPixel(uint8_t x, uint8_t y, PixelColor color = PixelColor::On) {
        if (x >= width() || y >= height()) {
            return;
        }

        const auto index = _bufferIndex(x, y);
        const uint8_t mask = static_cast<uint8_t>(1U << (y & 0x07U));
        switch (color) {
        case PixelColor::Off:
            _framebuffer[index] &= static_cast<uint8_t>(~mask);
            break;
        case PixelColor::Invert:
            _framebuffer[index] ^= mask;
            break;
        case PixelColor::On:
        default:
            _framebuffer[index] |= mask;
            break;
        }
    }

    void drawHorizontalLine(uint8_t x, uint8_t y, uint8_t length,
                            PixelColor color = PixelColor::On) {
        if (y >= height() || x >= width()) {
            return;
        }
        const auto endX =
            static_cast<uint8_t>(std::min<uint16_t>(width(), x + length));
        for (uint8_t cursor = x; cursor < endX; ++cursor) {
            setPixel(cursor, y, color);
        }
    }

    void drawVerticalLine(uint8_t x, uint8_t y, uint8_t length,
                          PixelColor color = PixelColor::On) {
        if (x >= width() || y >= height()) {
            return;
        }
        const auto endY =
            static_cast<uint8_t>(std::min<uint16_t>(height(), y + length));
        for (uint8_t cursor = y; cursor < endY; ++cursor) {
            setPixel(x, cursor, color);
        }
    }

    void drawRect(uint8_t x, uint8_t y, uint8_t rectWidth, uint8_t rectHeight,
                  PixelColor color = PixelColor::On) {
        if (rectWidth == 0 || rectHeight == 0) {
            return;
        }
        drawHorizontalLine(x, y, rectWidth, color);
        drawHorizontalLine(x, static_cast<uint8_t>(y + rectHeight - 1U),
                           rectWidth, color);
        drawVerticalLine(x, y, rectHeight, color);
        drawVerticalLine(static_cast<uint8_t>(x + rectWidth - 1U), y,
                         rectHeight, color);
    }

    void fillRect(uint8_t x, uint8_t y, uint8_t rectWidth, uint8_t rectHeight,
                  PixelColor color = PixelColor::On) {
        if (rectWidth == 0 || rectHeight == 0 || x >= width() ||
            y >= height()) {
            return;
        }
        const auto endX =
            static_cast<uint8_t>(std::min<uint16_t>(width(), x + rectWidth));
        const auto endY =
            static_cast<uint8_t>(std::min<uint16_t>(height(), y + rectHeight));
        for (uint8_t cursorX = x; cursorX < endX; ++cursorX) {
            drawVerticalLine(cursorX, y, static_cast<uint8_t>(endY - y),
                             color);
        }
    }

    void drawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1,
                  PixelColor color = PixelColor::On) {
        int16_t left = x0;
        int16_t top = y0;
        const int16_t right = x1;
        const int16_t bottom = y1;
        const int16_t dx = std::abs(right - left);
        const int16_t sx = left < right ? 1 : -1;
        const int16_t dy = static_cast<int16_t>(-std::abs(bottom - top));
        const int16_t sy = top < bottom ? 1 : -1;
        int16_t error = static_cast<int16_t>(dx + dy);

        for (;;) {
            setPixel(static_cast<uint8_t>(left), static_cast<uint8_t>(top),
                     color);
            if (left == right && top == bottom) {
                break;
            }
            const int16_t twiceError = static_cast<int16_t>(2 * error);
            if (twiceError >= dy) {
                error = static_cast<int16_t>(error + dy);
                left = static_cast<int16_t>(left + sx);
            }
            if (twiceError <= dx) {
                error = static_cast<int16_t>(error + dx);
                top = static_cast<int16_t>(top + sy);
            }
        }
    }

    ReturnCode flush() {
        FAIL_IF_INACTIVE_ERR("Cannot flush inactive SSD1306 display");
        return _flushRaw();
    }

    [[nodiscard]] uint8_t pages() const {
        return static_cast<uint8_t>(height() / 8U);
    }

  private:
    static constexpr uint8_t commandControlByte = 0x00;
    static constexpr uint8_t dataControlByte = 0x40;
    static constexpr size_t maxWidth = 128;
    static constexpr size_t maxHeight = 64;
    static constexpr size_t maxPages = maxHeight / 8U;
    static constexpr size_t framebufferSize = maxWidth * maxPages;

    ReturnCode _onBegin() {
        FAIL_IF(!_master.active(), ERR(CoreError, InvalidState),
                "Cannot begin SSD1306 display before I2C master is active");
        FAIL_IF_ERR_FWD(_device.begin(_master, config().device),
                        "Failed to register SSD1306 I2C device");
        auto ret = _initializePanel();
        if (!ret.ok()) {
            (void)_device.end();
            return ret;
        }
        if (config().clearOnBegin) {
            clear();
            FAIL_IF_ERR_FWD(_flushRaw(), "Failed to clear SSD1306 display");
        }
        _log_i("SSD1306 display initialized at 0x%02X (%ux%u)",
               config().device.address, config().width, config().height);
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        if (config().turnOffOnEnd && _device.active()) {
            ret.combine(_sendCommand(0xAE));
        }
        ret.combine(_device.end());
        clear();
        return ret;
    }

    ReturnCode _flushRaw() {
        FAIL_IF_ERR_FWD(_setAddressWindow(),
                        "Failed to set SSD1306 address window");

        const auto pageCount = pages();
        for (uint8_t page = 0; page < pageCount; ++page) {
            _pageBuffer[0] = dataControlByte;
            const auto offset = static_cast<size_t>(page) * width();
            std::copy_n(_framebuffer.data() + offset, width(),
                        _pageBuffer.data() + 1);
            const auto payload =
                std::span<const uint8_t>(_pageBuffer.data(), width() + 1U);
            FAIL_IF_ERR_FWD(_device.write(payload),
                            "Failed to write SSD1306 framebuffer page");
        }
        return OK();
    }

    ReturnCode _initializePanel() {
        const uint8_t multiplex = static_cast<uint8_t>(config().height - 1U);
        const uint8_t comPins = config().height == 32 ? 0x02 : 0x12;
        const uint8_t segmentRemap = config().flipHorizontal ? 0xA0 : 0xA1;
        const uint8_t comScan = config().flipVertical ? 0xC0 : 0xC8;

        const std::array<uint8_t, 28> commands{{
            0xAE,
            0xD5,
            0x80,
            0xA8,
            multiplex,
            0xD3,
            0x00,
            0x40,
            0x8D,
            0x14,
            0x20,
            0x00,
            segmentRemap,
            comScan,
            0xDA,
            comPins,
            0x81,
            config().contrast,
            0xD9,
            0xF1,
            0xDB,
            0x40,
            0xA4,
            0xA6,
            0x2E,
            0x21,
            0x00,
            static_cast<uint8_t>(config().width - 1U),
        }};
        FAIL_IF_ERR_FWD(_sendCommands(commands),
                        "Failed to send SSD1306 init command block");

        const std::array<uint8_t, 4> pageRange{{
            0x22,
            0x00,
            static_cast<uint8_t>(pages() - 1U),
            0xAF,
        }};
        return _sendCommands(pageRange);
    }

    ReturnCode _setAddressWindow() {
        const std::array<uint8_t, 6> commands{{
            0x21,
            0x00,
            static_cast<uint8_t>(width() - 1U),
            0x22,
            0x00,
            static_cast<uint8_t>(pages() - 1U),
        }};
        return _sendCommands(commands);
    }

    ReturnCode _sendCommand(uint8_t command) {
        const std::array<uint8_t, 2> payload{{commandControlByte, command}};
        return _device.write(payload);
    }

    ReturnCode _sendCommands(std::span<const uint8_t> commands) {
        FAIL_IF(commands.size() + 1U > _commandBuffer.size(),
                ERR(CoreError, Overflow),
                "SSD1306 command block is too large");
        _commandBuffer[0] = commandControlByte;
        std::copy(commands.begin(), commands.end(), _commandBuffer.begin() + 1);
        return _device.write(std::span<const uint8_t>(
            _commandBuffer.data(), commands.size() + 1U));
    }

    [[nodiscard]] size_t _bufferIndex(uint8_t x, uint8_t y) const {
        const auto page = static_cast<size_t>(y / 8U);
        return (page * width()) + x;
    }

    Master &_master;
    Device _device{};
    std::array<uint8_t, framebufferSize> _framebuffer{};
    std::array<uint8_t, maxWidth + 1U> _pageBuffer{};
    std::array<uint8_t, 32> _commandBuffer{};
};

} // namespace Totem::Wire::I2C::detail
