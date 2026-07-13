#pragma once

#include "AudioFft/Interfaces/Wire.hpp"
#include "LedDisplay/Animations/Registry.hpp"
#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include "LedDisplay/Interfaces/AnimationCommandCodec.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/AudioControls.hpp"
#include "LedDisplay/Primitives/Canvas.hpp"
#include "LedDisplay/detail/LayerStack.hpp"
#include "LedDisplay/detail/Metrics.hpp"
#include "LedTopology/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Mutex/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "Queue/Facade.hpp"
#include "Services/PubSub.hpp"
#include "Types/Angle.hpp"
#include "Types/Error.hpp"
#include "Wheel/Interfaces/Wire.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
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

    struct LayerFadeSwapState {
        bool active = false;
        Layer from = Layer::Fft;
        Layer to = Layer::FftAlt;
        uint32_t startMs = 0;
        uint16_t durationMs = 0;
    };

    enum class QueuedAnimationCommandType : uint8_t {
        None = 0,
        Play,
        Update,
        Stop,
        SetHueOffset,
        SetRotationOffset,
        SetBrightness,
        SetLayerActive,
        SetLayerOpacity,
        FadeLayerSwap,
    };

    struct QueuedAnimationCommand {
        static constexpr size_t payloadBytes = sizeof(AnimationPlayCommand);

        QueuedAnimationCommandType type = QueuedAnimationCommandType::None;
        std::array<std::byte, payloadBytes> payload{};

        template <typename Command>
        static QueuedAnimationCommand make(QueuedAnimationCommandType type,
                                           const Command &command) {
            static_assert(sizeof(Command) <= payloadBytes,
                          "Queued animation command payload is too small");
            QueuedAnimationCommand queued{.type = type};
            std::memcpy(queued.payload.data(), &command, sizeof(Command));
            return queued;
        }

        template <typename Command> [[nodiscard]] Command as() const {
            static_assert(sizeof(Command) <= payloadBytes,
                          "Queued animation command payload is too small");
            Command command{};
            std::memcpy(&command, payload.data(), sizeof(Command));
            return command;
        }
    };

    static_assert(sizeof(AnimationUpdateCommand) <=
                      QueuedAnimationCommand::payloadBytes);
    static_assert(sizeof(AnimationStopCommand) <=
                  QueuedAnimationCommand::payloadBytes);
    static_assert(sizeof(AnimationSetHueOffsetCommand) <=
                  QueuedAnimationCommand::payloadBytes);
    static_assert(sizeof(AnimationSetRotationOffsetCommand) <=
                  QueuedAnimationCommand::payloadBytes);
    static_assert(sizeof(AnimationSetBrightnessCommand) <=
                  QueuedAnimationCommand::payloadBytes);
    static_assert(sizeof(AnimationSetLayerActiveCommand) <=
                  QueuedAnimationCommand::payloadBytes);
    static_assert(sizeof(AnimationSetLayerOpacityCommand) <=
                  QueuedAnimationCommand::payloadBytes);
    static_assert(sizeof(AnimationFadeLayerSwapCommand) <=
                  QueuedAnimationCommand::payloadBytes);
    static_assert(std::is_trivially_copyable_v<QueuedAnimationCommand>,
                  "QueuedAnimationCommand must remain queue-copyable");

  public:
    ReturnCode begin() {
        prewarmMetrics();
        INIT_QUEUE_OR_FAIL(_commandQueue);
        _rebuildLogicalToLocalMap(rotationOffset());
        return OK();
    }

    ReturnCode end() {
        auto ret = OK();
        ret.combine(unsubscribePubSubInputs());
        DESTROY_QUEUE(ret, _commandQueue);
        return ret;
    }

    ReturnCode submit(const AnimationPlayCommand &cmd) {
        return _submit(QueuedAnimationCommandType::Play, cmd);
    }

    ReturnCode submit(const AnimationUpdateCommand &cmd) {
        return _submit(QueuedAnimationCommandType::Update, cmd);
    }

    ReturnCode submit(const AnimationStopCommand &cmd) {
        return _submit(QueuedAnimationCommandType::Stop, cmd);
    }

    ReturnCode submit(const AnimationSetHueOffsetCommand &cmd) {
        return _submit(QueuedAnimationCommandType::SetHueOffset, cmd);
    }

    ReturnCode submit(const AnimationSetRotationOffsetCommand &cmd) {
        return _submit(QueuedAnimationCommandType::SetRotationOffset, cmd);
    }

    ReturnCode submit(const AnimationSetBrightnessCommand &cmd) {
        return _submit(QueuedAnimationCommandType::SetBrightness, cmd);
    }

    ReturnCode submit(const AnimationSetLayerActiveCommand &cmd) {
        return _submit(QueuedAnimationCommandType::SetLayerActive, cmd);
    }

    ReturnCode submit(const AnimationSetLayerOpacityCommand &cmd) {
        return _submit(QueuedAnimationCommandType::SetLayerOpacity, cmd);
    }

    ReturnCode submit(const AnimationFadeLayerSwapCommand &cmd) {
        return _submit(QueuedAnimationCommandType::FadeLayerSwap, cmd);
    }

  private:
    template <typename Command>
    ReturnCode _submit(QueuedAnimationCommandType type, const Command &cmd) {
        if (_commandQueue == nullptr) {
            metrics().addQueueFailure();
            FAIL(ERR(CoreError, InvalidState),
                 "LED animation engine command queue is not ready");
        }
        if (!cmd.validate()) {
            metrics().addBadCommand();
            FAIL(ERR(CoreError, InvalidArgument),
                 "Invalid LED animation command");
        }
        const auto queued = QueuedAnimationCommand::make(type, cmd);
        auto ret = Totem::Queue::Platform::send(_commandQueue, &queued, 0);
        if (!ret.ok()) {
            metrics().addQueueFailure();
            _log_w("Dropped LED animation command because the command queue "
                   "is full: " ERR_FMT,
                   ERR_ARG(ret));
            return OK();
        }
        metrics().addCommand();
        return OK();
    }

  public:
    ReturnCode subscribePubSubInputs() {
        if (_pubSubInputSubscribed) {
            return OK();
        }
        FAIL_IF_NOT(PubSubService::configured(), ERR(CoreError, InvalidState),
                    "PubSub backend is not configured");

        auto &pubSub = PubSubService::get();
        auto fftSub = pubSub.subscribe(
            "led-fft", {.subscriber = this, .callback = onFftEnvelope},
            PubSubService::Topic::FftFrame);
        if (!fftSub) {
            return fftSub.error();
        }
        _fftSub = *fftSub;

        auto wheelSub = pubSub.subscribe(
            "led-wheel", {.subscriber = this, .callback = onWheelEnvelope},
            PubSubService::Topic::Wheel);
        if (!wheelSub) {
            (void)pubSub.unsubscribe(_fftSub);
            _fftSub = 0;
            return wheelSub.error();
        }
        _wheelSub = *wheelSub;

        auto peakSub = pubSub.subscribe(
            "led-peak", {.subscriber = this, .callback = onPeakEnvelope},
            PubSubService::Topic::Peak);
        if (!peakSub) {
            (void)pubSub.unsubscribe(_wheelSub);
            (void)pubSub.unsubscribe(_fftSub);
            _wheelSub = 0;
            _fftSub = 0;
            return peakSub.error();
        }
        _peakSub = *peakSub;
        _pubSubInputSubscribed = true;
        _log_i(
            "LED animation engine subscribed to FFT, peak, and wheel inputs");
        return OK();
    }

    ReturnCode unsubscribePubSubInputs() {
        if (!_pubSubInputSubscribed || !PubSubService::configured()) {
            _pubSubInputSubscribed = false;
            _fftSub = 0;
            _wheelSub = 0;
            _peakSub = 0;
            return OK();
        }

        auto ret = OK();
        auto &pubSub = PubSubService::get();
        if (_fftSub != 0) {
            ret.combine(pubSub.unsubscribe(_fftSub));
        }
        if (_wheelSub != 0) {
            ret.combine(pubSub.unsubscribe(_wheelSub));
        }
        if (_peakSub != 0) {
            ret.combine(pubSub.unsubscribe(_peakSub));
        }
        _fftSub = 0;
        _wheelSub = 0;
        _peakSub = 0;
        _pubSubInputSubscribed = false;
        return ret;
    }

    ReturnCode render(uint32_t nowMs, std::span<HsvColor> frame) {
        FAIL_IF_ERR_FWD(_drainCommands(nowMs),
                        "Failed to drain LED animation commands");

        _updateLayerFadeSwap(nowMs);
        _layers.beginFrame(_frames);
        const auto inputs = _snapshotInputs();
        const auto audioControls =
            _audioControls.update(inputs.fftFrame, inputs.hasFftFrame,
                                  inputs.peakEvent, inputs.hasPeakEvent);
        const auto hueOffset = _hueOffset.load(std::memory_order_relaxed);

        for (auto &slot : _animations) {
            if (!slot.active) {
                continue;
            }
            _render(slot, nowMs, hueOffset, inputs, audioControls);
        }

        _layers.compose(frame);
        _expireAnimations(nowMs);
        metrics().setActiveAnimations(_activeAnimationCount());
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

    [[nodiscard]] std::optional<uint8_t> takeBrightnessUpdate() {
        if (!_brightnessDirty.exchange(false, std::memory_order_acq_rel)) {
            return std::nullopt;
        }
        return _brightness.load(std::memory_order_acquire);
    }

    static ReturnCode
    onFftEnvelope(void *owner, const Totem::PubSubBackend::Envelope &envelope) {
        auto *self = static_cast<AnimationEngine *>(owner);
        FAIL_IF_NULL(self, ERR(CoreError, InvalidArgument),
                     "LED animation engine FFT subscriber owner is null");
        auto frame = envelope.getPayloadAs<Totem::AudioFft::FftFrame>();
        if (!frame) {
            metrics().addInputFailure();
            FAIL_ERR_FWD(frame.error(), "Failed to decode FFT frame");
        }
        return self->_captureFftFrame(*frame);
    }

    static ReturnCode
    onWheelEnvelope(void *owner,
                    const Totem::PubSubBackend::Envelope &envelope) {
        auto *self = static_cast<AnimationEngine *>(owner);
        FAIL_IF_NULL(self, ERR(CoreError, InvalidArgument),
                     "LED animation engine wheel subscriber owner is null");
        auto state = envelope.getPayloadAs<Totem::Wheel::WheelState>();
        if (!state) {
            metrics().addInputFailure();
            FAIL_ERR_FWD(state.error(), "Failed to decode wheel state");
        }
        return self->_captureWheelState(*state);
    }

    static ReturnCode
    onPeakEnvelope(void *owner,
                   const Totem::PubSubBackend::Envelope &envelope) {
        auto *self = static_cast<AnimationEngine *>(owner);
        FAIL_IF_NULL(self, ERR(CoreError, InvalidArgument),
                     "LED animation engine peak subscriber owner is null");
        auto event = envelope.getPayloadAs<Totem::AudioFft::PeakEvent>();
        if (!event) {
            metrics().addInputFailure();
            FAIL_ERR_FWD(event.error(), "Failed to decode peak event");
        }
        return self->_capturePeakEvent(*event);
    }

  private:
    ReturnCode _captureFftFrame(const Totem::AudioFft::FftFrame &frame) {
        Totem::Mutex::ScopedSpinlockGuard guard{_inputLock};
        _inputs.fftFrame = frame;
        _inputs.hasFftFrame = true;
        metrics().addFftInput();
        return OK();
    }

    ReturnCode _captureWheelState(const Totem::Wheel::WheelState &state) {
        Totem::Mutex::ScopedSpinlockGuard guard{_inputLock};
        _inputs.wheelState = state;
        _inputs.hasWheelState = true;
        metrics().addWheelInput();
        return OK();
    }

    ReturnCode _capturePeakEvent(const Totem::AudioFft::PeakEvent &event) {
        Totem::Mutex::ScopedSpinlockGuard guard{_inputLock};
        _inputs.peakEvent = event;
        _inputs.hasPeakEvent = true;
        return OK();
    }

    [[nodiscard]] AnimationInputSnapshot _snapshotInputs() const {
        Totem::Mutex::ScopedSpinlockGuard guard{_inputLock};
        return _inputs;
    }

    ReturnCode _drainCommands(uint32_t nowMs) {
        QueuedAnimationCommand cmd{};
        while (true) {
            auto result =
                Totem::Queue::Platform::receive(_commandQueue, &cmd, 0);
            if (!result.ok()) {
                if (result == ERR(Timeout)) {
                    return OK();
                }
                FAIL_ERR_FWD(result, "Failed to receive LED animation command");
            }
            const auto handled = _handleCommand(cmd, nowMs);
            if (!handled.ok()) {
                metrics().addBadCommand();
                _log_w("Dropped LED animation command type=%u: " ERR_FMT,
                       static_cast<unsigned>(cmd.type), ERR_ARG(handled));
            }
        }
    }

    ReturnCode _handleCommand(const QueuedAnimationCommand &queued,
                              uint32_t nowMs) {
        switch (queued.type) {
        case QueuedAnimationCommandType::None:
            FAIL(ERR(CoreError, InvalidArgument),
                 "LED animation command has no command type");
        case QueuedAnimationCommandType::Play: {
            const auto cmd = queued.as<AnimationPlayCommand>();
            if (!cmd.validate()) {
                metrics().addBadCommand();
                FAIL(ERR(CoreError, InvalidArgument),
                     "Invalid LED animation play command");
            }
            return _play(cmd, nowMs);
        }
        case QueuedAnimationCommandType::Update: {
            const auto cmd = queued.as<AnimationUpdateCommand>();
            if (!cmd.validate()) {
                metrics().addBadCommand();
                FAIL(ERR(CoreError, InvalidArgument),
                     "Invalid LED animation update command");
            }
            return _update(cmd);
        }
        case QueuedAnimationCommandType::Stop: {
            const auto cmd = queued.as<AnimationStopCommand>();
            _stop(cmd.requestId);
            metrics().addStop();
            return OK();
        }
        case QueuedAnimationCommandType::SetHueOffset:
            return _setHueOffset(queued.as<AnimationSetHueOffsetCommand>());
        case QueuedAnimationCommandType::SetRotationOffset:
            return _setRotationOffset(
                queued.as<AnimationSetRotationOffsetCommand>());
        case QueuedAnimationCommandType::SetBrightness:
            return _setBrightness(queued.as<AnimationSetBrightnessCommand>());
        case QueuedAnimationCommandType::SetLayerActive:
            return _setLayerActive(
                queued.as<AnimationSetLayerActiveCommand>());
        case QueuedAnimationCommandType::SetLayerOpacity:
            return _setLayerOpacity(
                queued.as<AnimationSetLayerOpacityCommand>());
        case QueuedAnimationCommandType::FadeLayerSwap:
            return _startLayerFadeSwap(
                queued.as<AnimationFadeLayerSwapCommand>(), nowMs);
        default:
            metrics().addBadCommand();
            FAIL(ERR(CoreError, InvalidArgument),
                 "Unknown queued LED animation command type");
        }
    }

    ReturnCode _update(const AnimationUpdateCommand &cmd) {
        FAIL_IF(cmd.payloadSize == 0, ERR(CoreError, InvalidArgument),
                "Animation update command has no payload");
        FAIL_IF(cmd.kind == AnimationKind::None,
                ERR(CoreError, InvalidArgument),
                "Animation update command has no animation kind");

        bool updated = false;
        for (auto &slot : _animations) {
            if (!slot.active) {
                continue;
            }
            if (cmd.requestId != 0 && slot.requestId != cmd.requestId) {
                continue;
            }
            if (Animations::kind(slot.payload) != cmd.kind) {
                continue;
            }
            FAIL_IF_ERR_FWD(Animations::update(slot.payload, cmd),
                            "Failed to update LED animation");
            updated = true;
        }
        if (!updated) {
            metrics().addUpdateMiss();
            _log_w("Dropped LED animation update kind=%u request=%u: no "
                   "matching active animation",
                   static_cast<unsigned>(cmd.kind), cmd.requestId);
        }
        metrics().addUpdate();
        return OK();
    }

    ReturnCode _play(const AnimationPlayCommand &cmd, uint32_t nowMs) {
        FAIL_IF(cmd.payloadSize == 0, ERR(CoreError, InvalidArgument),
                "Animation play command has no payload");
        FAIL_IF(static_cast<size_t>(cmd.layer) >= LayerStack::layerCount,
                ERR(CoreError, InvalidArgument),
                "Animation play command has invalid layer");

        FAIL_IF_UNEXPECTED_FWD(payload, Animations::makePayload(cmd),
                               "Failed to build animation payload");
        const auto existingSlotIndex = _findSlotByRequestId(cmd.requestId);
        const bool replacing = existingSlotIndex.has_value();
        size_t slotIndex = 0;
        if (replacing) {
            slotIndex = *existingSlotIndex;
            ++_animations[slotIndex].generation;
        } else {
            FAIL_IF_UNEXPECTED_FWD(reservedSlotIndex, _reserveSlot(),
                                   "No free LED animation slot");
            slotIndex = reservedSlotIndex;
        }

        auto &slot = _animations[slotIndex];
        slot.active = true;
        slot.requestId = cmd.requestId == 0 ? _nextRequestId() : cmd.requestId;
        slot.startMs = nowMs;
        slot.lifetimeMs = cmd.lifetimeMs;
        slot.layer = cmd.layer;
        slot.payload = payload;

        _log_i("%s LED animation kind=%u request=%u lifetime=%ums "
               "layer=%u",
               replacing ? "Replaced" : "Started",
               static_cast<unsigned>(cmd.kind), slot.requestId, cmd.lifetimeMs,
               static_cast<unsigned>(cmd.layer));
        metrics().addPlay();
        return OK();
    }

    [[nodiscard]] std::optional<size_t>
    _findSlotByRequestId(uint16_t requestId) const {
        if (requestId == 0) {
            return std::nullopt;
        }
        for (size_t i = 0; i < _animations.size(); ++i) {
            if (_animations[i].active &&
                _animations[i].requestId == requestId) {
                return i;
            }
        }
        return std::nullopt;
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

    uint32_t _stopLayer(Layer layer) {
        uint32_t stopped = 0;
        for (auto &slot : _animations) {
            if (!slot.active || slot.layer != layer) {
                continue;
            }
            slot.active = false;
            ++slot.generation;
            ++stopped;
        }
        return stopped;
    }

    ReturnCode _setHueOffset(const AnimationSetHueOffsetCommand &cmd) {
        _hueOffset.store(cmd.offset, std::memory_order_relaxed);
        _log_i("Set LED hue offset raw=%u",
               static_cast<unsigned>(cmd.offset));
        return OK();
    }

    ReturnCode _setRotationOffset(const AnimationSetRotationOffsetCommand &cmd) {
        const auto offset = Angle<uint8_t>::fromRaw(cmd.offset);
        _rotationOffset.store(offset.value, std::memory_order_relaxed);
        _rebuildLogicalToLocalMap(offset);
        _log_i("Set LED rotation offset raw=%u spokeOffset=%u",
               static_cast<unsigned>(offset.value),
               static_cast<unsigned>(_rotationSpokeOffset(offset)));
        return OK();
    }

    ReturnCode _setBrightness(const AnimationSetBrightnessCommand &cmd) {
        _brightness.store(cmd.value, std::memory_order_release);
        _brightnessDirty.store(true, std::memory_order_release);
        _log_i("Set LED display brightness=%u",
               static_cast<unsigned>(cmd.value));
        return OK();
    }

    ReturnCode _setLayerActive(const AnimationSetLayerActiveCommand &cmd) {
        FAIL_IF(static_cast<size_t>(cmd.layer) >= LayerStack::layerCount,
                ERR(CoreError, InvalidArgument),
                "Layer active command has invalid layer");
        _cancelLayerFadeSwapIfTouches(cmd.layer);
        _layers.setEnabled(cmd.layer, cmd.active);
        _log_i("Set LED layer active layer=%u active=%u",
               static_cast<unsigned>(cmd.layer), cmd.active);
        return OK();
    }

    ReturnCode _setLayerOpacity(const AnimationSetLayerOpacityCommand &cmd) {
        FAIL_IF(static_cast<size_t>(cmd.layer) >= LayerStack::layerCount,
                ERR(CoreError, InvalidArgument),
                "Layer opacity command has invalid layer");
        _cancelLayerFadeSwapIfTouches(cmd.layer);
        _layers.setOpacity(cmd.layer, cmd.opacity);
        _log_i("Set LED layer opacity layer=%u opacity=%u",
               static_cast<unsigned>(cmd.layer),
               static_cast<unsigned>(cmd.opacity));
        return OK();
    }

    ReturnCode _startLayerFadeSwap(const AnimationFadeLayerSwapCommand &cmd,
                                   uint32_t nowMs) {
        FAIL_IF(static_cast<size_t>(cmd.first) >= LayerStack::layerCount ||
                    static_cast<size_t>(cmd.second) >= LayerStack::layerCount,
                ERR(CoreError, InvalidArgument),
                "Layer fade swap command has invalid layer");
        FAIL_IF(cmd.first == cmd.second, ERR(CoreError, InvalidArgument),
                "Layer fade swap requires two distinct layers");
        FAIL_IF(cmd.durationMs == 0, ERR(CoreError, InvalidArgument),
                "Layer fade swap duration must be non-zero");
        FAIL_IF(_layerFadeSwap.active, ERR(CoreError, InvalidState),
                "Layer fade swap is already in progress");

        const auto firstOpacity = _layers.opacity(cmd.first);
        const auto secondOpacity = _layers.opacity(cmd.second);
        const auto firstEnabled = _layers.enabled(cmd.first);
        const auto secondEnabled = _layers.enabled(cmd.second);
        Layer from = Layer::Effect;
        Layer to = Layer::Effect;
        if (firstOpacity == layerFullOpacity && secondOpacity == 0) {
            from = cmd.first;
            to = cmd.second;
        } else if (firstOpacity == 0 && secondOpacity == layerFullOpacity) {
            from = cmd.second;
            to = cmd.first;
        } else if (firstEnabled != secondEnabled) {
            from = firstEnabled ? cmd.first : cmd.second;
            to = firstEnabled ? cmd.second : cmd.first;
        } else {
            FAIL(ERR(CoreError, InvalidState),
                 "Layer fade swap requires one layer at opacity 255 and the "
                 "other at opacity 0");
        }
        if (!_layers.enabled(from)) {
            _log_w("Starting LED layer fade swap from disabled source layer=%u",
                   static_cast<unsigned>(from));
        }

        _layers.setEnabled(from, true);
        _layers.setEnabled(to, true);
        _layers.setOpacity(from, layerFullOpacity);
        _layers.setOpacity(to, 0);
        _layerFadeSwap = LayerFadeSwapState{
            .active = true,
            .from = from,
            .to = to,
            .startMs = nowMs,
            .durationMs = cmd.durationMs,
        };
        _log_i("Started LED layer fade swap from=%u to=%u duration=%ums",
               static_cast<unsigned>(from), static_cast<unsigned>(to),
               static_cast<unsigned>(cmd.durationMs));
        return OK();
    }

    void _cancelLayerFadeSwapIfTouches(Layer layer) {
        if (!_layerFadeSwap.active ||
            (_layerFadeSwap.from != layer && _layerFadeSwap.to != layer)) {
            return;
        }
        _layerFadeSwap.active = false;
        _log_i("Cancelled LED layer fade swap touching layer=%u",
               static_cast<unsigned>(layer));
    }

    void _updateLayerFadeSwap(uint32_t nowMs) {
        if (!_layerFadeSwap.active) {
            return;
        }

        const auto elapsed = nowMs - _layerFadeSwap.startMs;
        if (elapsed >= _layerFadeSwap.durationMs) {
            _completeLayerFadeSwap();
            return;
        }

        constexpr uint32_t full = layerFullOpacity;
        const auto fadeIn =
            ((elapsed * full) + (_layerFadeSwap.durationMs / 2U)) /
            _layerFadeSwap.durationMs;
        const auto toOpacity = static_cast<uint8_t>(fadeIn);
        _layers.setOpacity(_layerFadeSwap.to, toOpacity);
        _layers.setOpacity(_layerFadeSwap.from,
                           static_cast<uint8_t>(full - fadeIn));
    }

    void _completeLayerFadeSwap() {
        const auto swap = _layerFadeSwap;
        _layerFadeSwap.active = false;
        _layers.setOpacity(swap.to, layerFullOpacity);
        _layers.setOpacity(swap.from, 0);
        _layers.setEnabled(swap.to, true);
        _layers.setEnabled(swap.from, false);
        const auto stopped = _stopLayer(swap.from);
        _log_i("Completed LED layer fade swap from=%u to=%u stopped=%u",
               static_cast<unsigned>(swap.from), static_cast<unsigned>(swap.to),
               static_cast<unsigned>(stopped));
    }

    void _render(const ActiveAnimation &slot, uint32_t nowMs, uint8_t hueOffset,
                 const AnimationInputSnapshot &inputs,
                 Primitives::AudioControls audioControls) {
        if (!_layers.enabled(slot.layer)) {
            return;
        }
        const uint32_t elapsed = nowMs - slot.startMs;
        _layers.clearScratch();
        if (Animations::requiresFullFrame(slot.payload)) {
            _renderFullFrame(slot, nowMs, elapsed, hueOffset, inputs,
                             audioControls);
            return;
        }

        auto canvas = Primitives::Canvas{_layers.scratch(), _logicalToLocal};
        auto ctx = AnimationRenderContext{
            .clock = {.nowMs = nowMs,
                      .elapsedMs = elapsed,
                      .durationMs = slot.lifetimeMs,
                      .frame = _frames},
            .hueOffset = hueOffset,
            .canvas = canvas,
            .inputs = inputs,
            .audio = audioControls,
        };
        Animations::render(slot.payload, ctx);
        _layers.blendScratch(slot.layer, Animations::style(slot.payload));
    }

    void _renderFullFrame(const ActiveAnimation &slot, uint32_t nowMs,
                          uint32_t elapsed, uint8_t hueOffset,
                          const AnimationInputSnapshot &inputs,
                          Primitives::AudioControls audioControls) {
        Compositor::clear(_fullFrameScratch);
        auto canvas =
            Primitives::Canvas{_fullFrameScratch, _identityLogicalToLocal};
        auto ctx = AnimationRenderContext{
            .clock = {.nowMs = nowMs,
                      .elapsedMs = elapsed,
                      .durationMs = slot.lifetimeMs,
                      .frame = _frames},
            .hueOffset = hueOffset,
            .canvas = canvas,
            .inputs = inputs,
            .audio = audioControls,
        };
        Animations::render(slot.payload, ctx);
        Primitives::projectLogicalFrameToOwned(
            _fullFrameScratch, _layers.scratch(), _logicalToLocal);
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

    [[nodiscard]] uint32_t _activeAnimationCount() const {
        uint32_t count = 0;
        for (const auto &slot : _animations) {
            if (slot.active) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] static uint8_t _rotationSpokeOffset(Angle<uint8_t> offset) {
        constexpr uint32_t angleSteps = 256U;
        constexpr uint32_t roundToNearestBias = angleSteps / 2U;
        const auto scaled =
            (static_cast<uint32_t>(offset.value) * Config::spokeCount) +
            roundToNearestBias;
        return static_cast<uint8_t>((scaled / angleSteps) % Config::spokeCount);
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
                _logicalToLocal[Primitives::Canvas::logicalIndex(
                    spoke, radial)] = local;
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
    std::array<LedTopology::LocalPixelIndex, Config::totalPixelCount>
        _identityLogicalToLocal{Primitives::identityLogicalToLocalMap()};
    std::array<HsvColor, Config::totalPixelCount> _fullFrameScratch{};
    std::array<ActiveAnimation, Config::maxActiveAnimations> _animations{};
    AnimationInputSnapshot _inputs{};
    Primitives::AudioControlSmoother _audioControls{};
    LayerFadeSwapState _layerFadeSwap{};
    std::atomic<uint8_t> _hueOffset{0};
    std::atomic<uint8_t> _rotationOffset{0};
    std::atomic<uint8_t> _brightness{0};
    std::atomic<bool> _brightnessDirty{false};
    uint16_t _lastRequestId = 0;
    uint32_t _frames = 0;
    Totem::PubSubBackend::SubscriberKey _fftSub = 0;
    Totem::PubSubBackend::SubscriberKey _wheelSub = 0;
    Totem::PubSubBackend::SubscriberKey _peakSub = 0;
    bool _pubSubInputSubscribed = false;
    mutable ::platform::Spinlock _inputLock = ::platform::create_spinlock();

    STANDARD_QUEUE(_commandQueue, QueuedAnimationCommand, Config::commandQueueSize)
};

} // namespace Totem::LedDisplay::detail
