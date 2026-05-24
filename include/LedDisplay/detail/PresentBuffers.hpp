#pragma once

#include "LedDisplay/Interfaces/Color.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Interfaces/PresentBufferMode.hpp"
#include <array>
#include <cstddef>
#include <limits>
#include <span>

namespace Totem::LedDisplay::detail {

class PresentBuffers {
  public:
    static constexpr PresentBufferMode mode = Config::presentBufferMode;
    static constexpr size_t bufferCount = presentBufferCount(mode);
    static constexpr size_t invalidIndex = std::numeric_limits<size_t>::max();

    struct PresentResult {
        std::span<const HsvColor> frame{};
        bool repeated = false;
    };

    [[nodiscard]] std::span<HsvColor> renderTarget() {
        return _frames[_renderIndex];
    }

    void publishRenderedFrame() {
        if constexpr (bufferCount == 1) {
            _presentIndex = _renderIndex;
            _hasPresented = true;
            return;
        }

        _readyIndex = _renderIndex;
        _hasReady = true;
    }

    [[nodiscard]] bool hasPresentedFrame() const { return _hasPresented; }

    [[nodiscard]] PresentResult selectForPresent() {
        if constexpr (bufferCount == 1) {
            return PresentResult{.frame = _frames[_presentIndex],
                                 .repeated = false};
        }

        if (_hasReady) {
            _presentIndex = _readyIndex;
            _readyIndex = invalidIndex;
            _hasReady = false;
            _hasPresented = true;
            _renderIndex = _nextRenderIndex();
            return PresentResult{.frame = _frames[_presentIndex],
                                 .repeated = false};
        }

        if (!_hasPresented) {
            return PresentResult{};
        }

        return PresentResult{.frame = _frames[_presentIndex],
                             .repeated = true};
    }

  private:
    [[nodiscard]] size_t _nextRenderIndex() const {
        if constexpr (bufferCount == 1) {
            return _renderIndex;
        }

        for (size_t candidate = 0; candidate < bufferCount; ++candidate) {
            if (candidate == _presentIndex) {
                continue;
            }
            if (_hasReady && candidate == _readyIndex) {
                continue;
            }
            return candidate;
        }

        return _renderIndex;
    }

    using Frame = std::array<HsvColor, Config::ownedPixelCount>;

    std::array<Frame, bufferCount> _frames{};
    size_t _renderIndex = bufferCount > 1 ? 1 : 0;
    size_t _presentIndex = 0;
    size_t _readyIndex = invalidIndex;
    bool _hasReady = false;
    bool _hasPresented = false;
};

} // namespace Totem::LedDisplay::detail
