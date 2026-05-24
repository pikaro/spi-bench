#pragma once

#include "Audio/Interfaces/Wire.hpp"
#include "LedDisplay/Animations/Registry.hpp"
#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include "LedDisplay/Interfaces/AnimationCommandCodec.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/Canvas.hpp"
#include "LedDisplay/detail/LayerStack.hpp"
#include "LedTopology/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Mutex/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "Queue/Facade.hpp"
#include "Types/Angle.hpp"
#include "Types/Error.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace Totem::LedDisplay::detail {

class AnimationEngine {
    struct ActiveAnimation {
        bool active = false;
        uint16_t generation = 0;
        uint16_t requestId = 0;
        uint32_t startMs = 0;
        uint16_t lifetimeMs = 0;
        Layer layer = Layer::Effect;
        Animations::Payload payload{};
    };

  public:
    ReturnCode begin() {
        INIT_QUEUE_OR_FAIL(_commandQueue);
        _rebuildLogicalToLocalMap(rotationOffset());
        return OK();
    }

    ReturnCode end() {
        auto ret = OK();
        DESTROY_QUEUE(ret, _commandQueue);
        return ret;
    }

    ReturnCode submit(const AnimationCommand &cmd) {
        FAIL_IF(_commandQueue == nullptr, ERR(CoreError, InvalidState),
                "LED animation engine command queue is not ready");
        FAIL_IF_NOT(cmd.validate(), ERR(CoreError, InvalidArgument),
                    "Invalid LED animation command");
        auto ret = Totem::Queue::Platform::send(_commandQueue, &cmd);
        FAIL_IF_ERR_FWD(ret, "Failed to enqueue LED animation command");
        return OK();
    }

    ReturnCode render(uint32_t nowMs, std::span<HsvColor> frame) {
        FAIL_IF_ERR_FWD(_drainCommands(nowMs),
                        "Failed to drain LED animation commands");

        _layers.beginFrame(_frames);
        const auto inputs = _snapshotInputs();
        const auto hueOffset = _hueOffset.load(std::memory_order_relaxed);

        for (auto &slot : _animations) {
            if (!slot.active) {
                continue;
            }
            _render(slot, nowMs, hueOffset, inputs);
        }

        _layers.compose(frame);
        _expireAnimations(nowMs);
        ++_frames;
        return OK();
    }

    [[nodiscard]] Angle<uint8_t> hueOffset() const {
        return Angle<uint8_t>::fromRaw(
            _hueOffset.load(std::memory_order_relaxed));
    }

    [[nodiscard]] Angle<uint8_t> rotationOffset() const {
        return Angle<uint8_t>::fromRaw(
            _rotationOffset.load(std::memory_order_relaxed));
    }

    static ReturnCode
    onFftEnvelope(void *owner,
                  const Totem::PubSubBackend::Envelope &envelope) {
        auto *self = static_cast<AnimationEngine *>(owner);
        FAIL_IF_NULL(self, ERR(CoreError, InvalidArgument),
                     "LED animation engine FFT subscriber owner is null");
        FAIL_IF_UNEXPECTED_FWD(frame,
                               envelope.getPayloadAs<Totem::Audio::FftFrame>(),
                               "Failed to decode FFT frame");
        return self->_captureFftFrame(frame);
    }

  private:
    ReturnCode _captureFftFrame(const Totem::Audio::FftFrame &frame) {
        Totem::Mutex::ScopedSpinlockGuard guard{_inputLock};
        _inputs.fftFrame = frame;
        _inputs.hasFftFrame = true;
        return OK();
    }

    [[nodiscard]] AnimationInputSnapshot _snapshotInputs() const {
        Totem::Mutex::ScopedSpinlockGuard guard{_inputLock};
        return _inputs;
    }

    ReturnCode _drainCommands(uint32_t nowMs) {
        AnimationCommand cmd{};
        while (true) {
            auto result =
                Totem::Queue::Platform::receive(_commandQueue, &cmd, 0);
            if (!result.ok()) {
                if (result == ERR(Timeout)) {
                    return OK();
                }
                FAIL_ERR_FWD(result, "Failed to receive LED animation command");
            }
            FAIL_IF_ERR_FWD(_handleCommand(cmd, nowMs),
                            "Failed to handle LED animation command");
        }
    }

    ReturnCode _handleCommand(const AnimationCommand &cmd, uint32_t nowMs) {
        FAIL_IF_NOT(cmd.validate(), ERR(CoreError, InvalidArgument),
                    "Invalid LED animation command");
        switch (cmd.type) {
        case AnimationCommandType::None:
            FAIL(ERR(CoreError, InvalidArgument),
                 "LED animation command has no command type");
        case AnimationCommandType::Play:
            return _play(cmd, nowMs);
        case AnimationCommandType::Stop:
            _stop(cmd.requestId);
            return OK();
        case AnimationCommandType::SetHueOffset:
            return _setHueOffset(cmd);
        case AnimationCommandType::SetRotationOffset:
            return _setRotationOffset(cmd);
        default:
            FAIL(ERR(CoreError, InvalidArgument),
                 "Unknown LED animation command type");
        }
    }

    ReturnCode _play(const AnimationCommand &cmd, uint32_t nowMs) {
        FAIL_IF(cmd.payloadSize == 0, ERR(CoreError, InvalidArgument),
                "Animation play command has no payload");
        FAIL_IF(static_cast<size_t>(cmd.layer) >= LayerStack::layerCount,
                ERR(CoreError, InvalidArgument),
                "Animation play command has invalid layer");

        FAIL_IF_UNEXPECTED_FWD(payload, Animations::makePayload(cmd),
                               "Failed to build animation payload");
        FAIL_IF_UNEXPECTED_FWD(slotIndex, _reserveSlot(),
                               "No free LED animation slot");

        auto &slot = _animations[slotIndex];
        slot.active = true;
        slot.requestId =
            cmd.requestId == 0 ? _nextRequestId() : cmd.requestId;
        slot.startMs = nowMs;
        slot.lifetimeMs = cmd.lifetimeMs;
        slot.layer = cmd.layer;
        slot.payload = payload;

        _log_i("Started LED animation kind=%u request=%u lifetime=%ums "
               "layer=%u",
               static_cast<unsigned>(cmd.kind), slot.requestId,
               cmd.lifetimeMs, static_cast<unsigned>(cmd.layer));
        return OK();
    }

    std::expected<size_t, ReturnCode> _reserveSlot() {
        for (size_t i = 0; i < _animations.size(); ++i) {
            if (!_animations[i].active) {
                ++_animations[i].generation;
                return i;
            }
        }
        return std::unexpected(ERR(CoreError, OutOfMemory));
    }

    void _stop(uint16_t requestId) {
        for (auto &slot : _animations) {
            if (!slot.active) {
                continue;
            }
            if (requestId == 0 || slot.requestId == requestId) {
                slot.active = false;
                ++slot.generation;
            }
        }
    }

    ReturnCode _setHueOffset(const AnimationCommand &cmd) {
        FAIL_IF_UNEXPECTED_FWD(offset,
                               decodeCommandPayload<Angle<uint8_t>>(cmd),
                               "Failed to decode LED hue offset");
        _hueOffset.store(offset.value, std::memory_order_relaxed);
        _log_i("Set LED hue offset raw=%u",
               static_cast<unsigned>(offset.value));
        return OK();
    }

    ReturnCode _setRotationOffset(const AnimationCommand &cmd) {
        FAIL_IF_UNEXPECTED_FWD(offset,
                               decodeCommandPayload<Angle<uint8_t>>(cmd),
                               "Failed to decode LED rotation offset");
        _rotationOffset.store(offset.value, std::memory_order_relaxed);
        _rebuildLogicalToLocalMap(offset);
        _log_i("Set LED rotation offset raw=%u spokeOffset=%u",
               static_cast<unsigned>(offset.value),
               static_cast<unsigned>(_rotationSpokeOffset(offset)));
        return OK();
    }

    void _render(const ActiveAnimation &slot, uint32_t nowMs,
                 uint8_t hueOffset,
                 const AnimationInputSnapshot &inputs) {
        const uint32_t elapsed = nowMs - slot.startMs;
        _layers.clearScratch(slot.layer);
        auto canvas =
            Primitives::Canvas{_layers.scratch(slot.layer), _logicalToLocal};
        auto ctx = AnimationRenderContext{
            .clock = {.nowMs = nowMs,
                      .elapsedMs = elapsed,
                      .durationMs = slot.lifetimeMs,
                      .frame = _frames},
            .hueOffset = hueOffset,
            .canvas = canvas,
            .inputs = inputs,
        };
        Animations::render(slot.payload, ctx);
        _layers.blendScratch(slot.layer, Animations::style(slot.payload));
    }

    void _expireAnimations(uint32_t nowMs) {
        for (auto &slot : _animations) {
            if (!slot.active || !_expired(slot, nowMs)) {
                continue;
            }
            slot.active = false;
            ++slot.generation;
        }
    }

    [[nodiscard]] static bool _expired(const ActiveAnimation &slot,
                                       uint32_t nowMs) {
        if (slot.lifetimeMs == 0) {
            return false;
        }
        return (nowMs - slot.startMs) >= slot.lifetimeMs;
    }

    [[nodiscard]] static uint8_t
    _rotationSpokeOffset(Angle<uint8_t> offset) {
        constexpr uint32_t angleSteps = 256U;
        constexpr uint32_t roundToNearestBias = angleSteps / 2U;
        const auto scaled =
            (static_cast<uint32_t>(offset.value) * Config::spokeCount) +
            roundToNearestBias;
        return static_cast<uint8_t>((scaled / angleSteps) %
                                    Config::spokeCount);
    }

    void _rebuildLogicalToLocalMap(Angle<uint8_t> rotationOffset) {
        const auto spokeOffset = _rotationSpokeOffset(rotationOffset);
        for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
            const auto rotatedSpoke = static_cast<uint8_t>(
                (static_cast<uint32_t>(spoke) + spokeOffset) %
                Config::spokeCount);
            for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
                const auto physical =
                    LedTopology::Umbrella::physicalFor(rotatedSpoke, radial);
                auto local = Primitives::Canvas::invalidLocalPixel;
                if constexpr (Config::ledGroupCount > 1) {
                    if (LedTopology::OwnedPixels::owns(physical)) {
                        local = LedTopology::OwnedPixels::localIndex(physical);
                    }
                } else {
                    local = LedTopology::OwnedPixels::localIndex(physical);
                }
                _logicalToLocal[Primitives::Canvas::logicalIndex(spoke,
                                                                 radial)] =
                    local;
            }
        }
    }

    uint16_t _nextRequestId() {
        ++_lastRequestId;
        if (_lastRequestId == 0) {
            ++_lastRequestId;
        }
        return _lastRequestId;
    }

    LayerStack _layers{};
    std::array<LedTopology::LocalPixelIndex, Config::totalPixelCount>
        _logicalToLocal{};
    std::array<ActiveAnimation, Config::maxActiveAnimations> _animations{};
    AnimationInputSnapshot _inputs{};
    std::atomic<uint8_t> _hueOffset{0};
    std::atomic<uint8_t> _rotationOffset{0};
    uint16_t _lastRequestId = 0;
    uint32_t _frames = 0;
    mutable ::platform::Spinlock _inputLock = ::platform::create_spinlock();

    STANDARD_QUEUE(_commandQueue, AnimationCommand, Config::commandQueueSize)
};

} // namespace Totem::LedDisplay::detail
