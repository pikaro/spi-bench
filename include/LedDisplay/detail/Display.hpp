#pragma once

#include "Audio/Interfaces/Wire.hpp"
#include "Base/HasCommands.hpp"
#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Interfaces/Types.hpp"
#include "LedDisplay/Outputs/FastLedOutput.hpp"
#include "LedDisplay/detail/Commands.hpp"
#include "LedDisplay/detail/Primitives.hpp"
#include "LedDisplay/detail/RendererSelect.hpp"
#include "LedTopology/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Mutex/Facade.hpp"
#include "Platform/Gpio.hpp"
#include "PubSubBackend/detail/Codec.hpp"
#include "Queue/Facade.hpp"
#include "Services/PubSub.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "Types/Error.hpp"
#include "Types/Gpio.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <variant>

namespace Totem::LedDisplay::detail {

class Display : public HasLifecycle<Display, Config>,
                public HasTaskController<Display, Config>,
                public HasCommands<Display, Commands<Display>> {
    friend class HasLifecycle<Display, Config>;
    friend struct LifecycleContract<Display, Config>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<Display, Config>;
    friend struct TaskController::TaskHooks::Contract<Display>;
    friend struct TaskControllerContract<Display>;

    struct ActiveAnimation {
        bool active = false;
        uint16_t generation = 0;
        uint16_t requestId = 0;
        uint32_t startMs = 0;
        uint16_t lifetimeMs = 0;
        AnimationKind kind = AnimationKind::None;
        AnimationPayload payload{DiagnosticFill{}};
    };

  public:
    explicit Display(TaskController::IRegistry &registry)
        : HasTaskController<Display, Config>(registry) {}

    DELETE_COPY(Display)
    DELETE_MOVE(Display)

    static constexpr const char *name = "LedDisplay";
    static constexpr LogComponent logComponent = LogComponent::Output;

    ReturnCode playDefaultWave() {
        auto cmd = AnimationCommand{
            .type = AnimationCommandType::Play,
            .kind = AnimationKind::CenterWave,
            .requestId = _nextRequestId(),
            .layer = Layer::Main,
            .lifetimeMs = 1200,
        };
        const auto config = CenterWaveConfig{
            .hue = 144,
            .saturation = 255,
            .value = 180,
            .width = 5,
        };
        FAIL_IF_ERR_FWD(_encodePayload(cmd, config),
                        "Failed to encode default center wave config");
        FAIL_IF_ERR_FWD(_enqueue(cmd),
                        "Failed to enqueue default center wave animation");
        _log_i("Queued default LED center wave request=%u", cmd.requestId);
        return OK();
    }

    ReturnCode playPrimitive(PrimitiveKind primitive) {
        auto cmd = AnimationCommand{
            .type = AnimationCommandType::Play,
            .kind = AnimationKind::PrimitiveDemo,
            .requestId = _nextRequestId(),
            .layer = Layer::Main,
            .lifetimeMs = 2400,
        };
        const auto config = PrimitiveDemoConfig{
            .primitive = primitive,
            .hue = 144,
            .saturation = 255,
            .value = 180,
            .width = 5,
            .density = 56,
            .speed = 160,
        };
        FAIL_IF_ERR_FWD(_encodePayload(cmd, config),
                        "Failed to encode primitive demo config");
        FAIL_IF_ERR_FWD(_enqueue(cmd),
                        "Failed to enqueue primitive demo animation");
        _log_i("Queued LED primitive demo primitive=%u request=%u",
               static_cast<unsigned>(primitive), cmd.requestId);
        return OK();
    }

    ReturnCode setHueOffset(Angle<uint8_t> offset) {
        auto cmd = AnimationCommand{
            .type = AnimationCommandType::SetHueOffset,
            .kind = AnimationKind::None,
            .requestId = _nextRequestId(),
            .layer = Layer::Main,
            .lifetimeMs = 0,
        };
        FAIL_IF_ERR_FWD(_encodePayload(cmd, offset),
                        "Failed to encode LED hue offset command");
        FAIL_IF_ERR_FWD(_enqueue(cmd), "Failed to enqueue LED hue offset");
        return OK();
    }

    [[nodiscard]] Angle<uint8_t> hueOffset() const {
        return Angle<uint8_t>::fromRaw(
            _hueOffset.load(std::memory_order_relaxed));
    }

    ReturnCode setRotationOffset(Angle<uint8_t> offset) {
        auto cmd = AnimationCommand{
            .type = AnimationCommandType::SetRotationOffset,
            .kind = AnimationKind::None,
            .requestId = _nextRequestId(),
            .layer = Layer::Main,
            .lifetimeMs = 0,
        };
        FAIL_IF_ERR_FWD(_encodePayload(cmd, offset),
                        "Failed to encode LED rotation offset command");
        FAIL_IF_ERR_FWD(_enqueue(cmd), "Failed to enqueue LED rotation offset");
        return OK();
    }

    [[nodiscard]] Angle<uint8_t> rotationOffset() const {
        return Angle<uint8_t>::fromRaw(
            _rotationOffset.load(std::memory_order_relaxed));
    }

    ReturnCode subscribePubSub() {
        if (_pubSubSubscribed) {
            return OK();
        }
        FAIL_IF_NOT(PubSubService::configured(), ERR(CoreError, InvalidState),
                    "PubSub backend is not configured");

        auto &pubSub = PubSubService::get();
        auto animationSub = pubSub.subscribe(
            "led-anim", {.subscriber = this, .callback = _onAnimationEnvelope},
            PubSubService::Topic::Animation);
        if (!animationSub) {
            return animationSub.error();
        }
        _animationSub = *animationSub;

        auto fftSub = pubSub.subscribe(
            "led-fft", {.subscriber = this, .callback = _onFftEnvelope},
            PubSubService::Topic::FftFrame);
        if (!fftSub) {
            (void)pubSub.unsubscribe(_animationSub);
            _animationSub = 0;
            return fftSub.error();
        }
        _fftSub = *fftSub;
        _pubSubSubscribed = true;
        _log_i("LedDisplay subscribed to animation and FFT PubSub topics");
        return OK();
    }

    ReturnCode beginPresentStrobe(Pin pin,
                                  GpioPull pull = GpioPull::Down) {
        FAIL_IF_INACTIVE_ERR("Cannot initialize LED present strobe before %s "
                             "begins",
                             name);
        if (_presentStrobeEnabled) {
            return OK();
        }
        FAIL_IF_ERR_FWD(
            _presentStrobeGpio.initInput(pin, pull, GpioInterrupt::Rising),
            "Failed to initialize LED present strobe input");
        FAIL_IF_ERR_FWD(
            _presentStrobeGpio.registerIsr(this, _onPresentStrobeIsr),
            "Failed to register LED present strobe ISR");
        _presentStrobeEnabled = true;
        _log_i("LedDisplay present strobe input ready on pin " SV_FMT,
               MAGIC_SV_ARG(pin));
        return OK();
    }

  private:
    ReturnCode _onBegin() {
        static_assert(Config::dataLineCount <= 2,
                      "FastLED output currently supports two configured lines");
        DEFAULT_TASK();
        _renderTask = task;
        INIT_QUEUE_OR_FAIL(_commandQueue);
        _rebuildLogicalToLocalMap(rotationOffset());
        FAIL_IF_ERR_FWD(_output.begin(config()),
                        "Failed to initialize LED output backend");
        FAIL_IF_ERR_FWD(this->_registerCommands(),
                        "Failed to register LedDisplay commands");
        START_TASK();
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        ret.combine(_presentStrobeGpio.deinit());
        _presentStrobeEnabled = false;
        ret.combine(_unsubscribePubSub());
        ret.combine(this->_endTaskController());
        ret.combine(this->_deregisterCommands());
        ret.combine(_output.deinit());
        DESTROY_QUEUE(ret, _commandQueue);
        return ret;
    }

    ReturnCode _unsubscribePubSub() {
        if (!_pubSubSubscribed || !PubSubService::configured()) {
            _pubSubSubscribed = false;
            _animationSub = 0;
            _fftSub = 0;
            return OK();
        }
        auto ret = OK();
        auto &pubSub = PubSubService::get();
        if (_animationSub != 0) {
            ret.combine(pubSub.unsubscribe(_animationSub));
        }
        if (_fftSub != 0) {
            ret.combine(pubSub.unsubscribe(_fftSub));
        }
        _animationSub = 0;
        _fftSub = 0;
        _pubSubSubscribed = false;
        return ret;
    }

    static ReturnCode
    _onAnimationEnvelope(void *owner,
                         const Totem::PubSubBackend::Envelope &envelope) {
        auto *self = static_cast<Display *>(owner);
        FAIL_IF_NULL(self, ERR(CoreError, InvalidArgument),
                     "LedDisplay animation subscriber owner is null");
        FAIL_IF_UNEXPECTED_FWD(cmd, envelope.getPayloadAs<AnimationCommand>(),
                               "Failed to decode animation command");
        return self->_enqueue(cmd);
    }

    static ReturnCode
    _onFftEnvelope(void *owner,
                   const Totem::PubSubBackend::Envelope &envelope) {
        auto *self = static_cast<Display *>(owner);
        FAIL_IF_NULL(self, ERR(CoreError, InvalidArgument),
                     "LedDisplay FFT subscriber owner is null");
        FAIL_IF_UNEXPECTED_FWD(frame,
                               envelope.getPayloadAs<Totem::Audio::FftFrame>(),
                               "Failed to decode FFT frame");
        return self->_captureFftFrame(frame);
    }

    static void _onPresentStrobeIsr(void *owner, GpioEvent event) {
        auto *self = static_cast<Display *>(owner);
        if (self == nullptr || !event.level) {
            return;
        }
        self->_pendingPresentStrobes.fetch_add(1, std::memory_order_relaxed);
        Totem::TaskController::Controller::signalTaskFromIsr(
            self->_renderTask, Signal::Ready);
    }

    ReturnCode _captureFftFrame(const Totem::Audio::FftFrame &frame) {
        Totem::Mutex::ScopedSpinlockGuard guard{_inputLock};
        _latestFftFrame = frame;
        _hasFftFrame = true;
        return OK();
    }

    ReturnCode _onTaskStep() {
        const auto nowMs = ::platform::get_time();
        const auto nowUs = ::platform::get_time_us();
        if (_presentStrobeEnabled && !_consumePresentStrobe(nowMs)) {
            return OK();
        }
        _checkDitherCadence(nowMs, nowUs);

        _expireAnimations(nowMs);
        FAIL_IF_ERR_FWD(_drainCommands(nowMs),
                        "Failed to drain LED animation commands");

        _clearFrame();
        _renderAnimations(nowMs);

        const auto showStartUs = ::platform::get_time_us();
        FAIL_IF_ERR_FWD(_output.show(_frame),
                        "Failed to show LED output frame");
        const auto showUs = ::platform::get_time_us() - showStartUs;
        _maxShowUs = std::max(_maxShowUs, static_cast<uint32_t>(showUs));
        ++_frames;
        return OK();
    }

    [[nodiscard]] bool _consumePresentStrobe(uint32_t nowMs) {
        const auto pending =
            _pendingPresentStrobes.exchange(0, std::memory_order_acq_rel);
        if (pending == 0) {
            return false;
        }
        ++_presentStrobeFrames;
        if (pending <= 1) {
            return true;
        }

        _missedPresentStrobes += pending - 1U;
        if (nowMs - _lastPresentMissLogMs >= 1000U) {
            _lastPresentMissLogMs = nowMs;
            _log_e("LED render missed %lu present strobes; pending=%lu "
                   "totalMissed=%lu",
                   static_cast<unsigned long>(pending - 1U),
                   static_cast<unsigned long>(pending),
                   static_cast<unsigned long>(_missedPresentStrobes));
        }
        return true;
    }

    ReturnCode _enqueue(const AnimationCommand &cmd) {
        FAIL_IF_INACTIVE_ERR("Cannot enqueue LED animation before %s begins",
                             name);
        FAIL_IF_NOT(cmd.validate(), ERR(CoreError, InvalidArgument),
                    "Invalid LED animation command");
        auto ret = Totem::Queue::Platform::send(_commandQueue, &cmd);
        FAIL_IF_ERR_FWD(ret, "Failed to enqueue LED animation command");
        return OK();
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

        FAIL_IF_UNEXPECTED_FWD(slotIndex, _reserveSlot(),
                               "No free LED animation slot");
        auto &slot = _animations[slotIndex];
        slot.active = true;
        slot.requestId = cmd.requestId;
        slot.startMs = nowMs;
        slot.lifetimeMs = cmd.lifetimeMs;
        slot.kind = cmd.kind;

        switch (cmd.kind) {
        case AnimationKind::None:
            slot.active = false;
            FAIL(ERR(CoreError, InvalidArgument),
                 "Animation play command has no animation kind");
        case AnimationKind::DiagnosticFill: {
            FAIL_IF_UNEXPECTED_FWD(config,
                                   _decodePayload<DiagnosticFillConfig>(cmd),
                                   "Failed to decode diagnostic fill config");
            slot.payload = AnimationPayload{DiagnosticFill{.config = config}};
            break;
        }
        case AnimationKind::CenterWave: {
            FAIL_IF_UNEXPECTED_FWD(config,
                                   _decodePayload<CenterWaveConfig>(cmd),
                                   "Failed to decode center wave config");
            slot.payload = AnimationPayload{CenterWave{.config = config}};
            break;
        }
        case AnimationKind::FftReactive: {
            FAIL_IF_UNEXPECTED_FWD(config,
                                   _decodePayload<FftReactiveConfig>(cmd),
                                   "Failed to decode FFT reactive config");
            slot.payload = AnimationPayload{FftReactive{.config = config}};
            break;
        }
        case AnimationKind::PrimitiveDemo: {
            FAIL_IF_UNEXPECTED_FWD(config,
                                   _decodePayload<PrimitiveDemoConfig>(cmd),
                                   "Failed to decode primitive demo config");
            slot.payload = AnimationPayload{PrimitiveDemo{.config = config}};
            break;
        }
        default:
            slot.active = false;
            FAIL(ERR(CoreError, InvalidArgument), "Unknown animation kind");
        }

        _log_i("Started LED animation kind=%u request=%u lifetime=%ums",
               static_cast<unsigned>(cmd.kind), cmd.requestId, cmd.lifetimeMs);
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
        FAIL_IF_UNEXPECTED_FWD(offset, _decodePayload<Angle<uint8_t>>(cmd),
                               "Failed to decode LED hue offset");
        _hueOffset.store(offset.value, std::memory_order_relaxed);
        _log_i("Set LED hue offset raw=%u", static_cast<unsigned>(offset.value));
        return OK();
    }

    ReturnCode _setRotationOffset(const AnimationCommand &cmd) {
        FAIL_IF_UNEXPECTED_FWD(offset, _decodePayload<Angle<uint8_t>>(cmd),
                               "Failed to decode LED rotation offset");
        _rotationOffset.store(offset.value, std::memory_order_relaxed);
        _rebuildLogicalToLocalMap(offset);
        _log_i("Set LED rotation offset raw=%u spokeOffset=%u",
               static_cast<unsigned>(offset.value),
               static_cast<unsigned>(_rotationSpokeOffset(offset)));
        return OK();
    }

    void _renderAnimations(uint32_t nowMs) {
        const auto hueOffset = _hueOffset.load(std::memory_order_relaxed);
        for (auto &slot : _animations) {
            if (!slot.active) {
                continue;
            }
            std::visit(
                [this, &slot, nowMs, hueOffset](const auto &animation) {
                    _render(animation, slot, nowMs, hueOffset);
                },
                slot.payload);
        }
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

    void _render(const DiagnosticFill &animation,
                 const ActiveAnimation & /*unused*/, uint32_t /*unused*/,
                 uint8_t hueOffset) {
        const auto color = HsvColor{
            .hue = static_cast<uint8_t>(animation.config.hue + hueOffset),
            .saturation = animation.config.saturation,
            .value = animation.config.value};
        for (auto &pixel : _frame) {
            pixel = color;
        }
    }

    void _render(const CenterWave &animation, const ActiveAnimation &slot,
                 uint32_t nowMs, uint8_t hueOffset) {
        const uint32_t elapsed = nowMs - slot.startMs;
        const uint32_t duration =
            slot.lifetimeMs == 0 ? 1200U : slot.lifetimeMs;
        auto canvas = PrimitiveCanvas{_frame, _logicalToLocal};
        drawCenterWave(canvas, PrimitiveParams{
                                   .elapsedMs = elapsed,
                                   .durationMs = duration,
                                   .hue = static_cast<uint8_t>(
                                       animation.config.hue + hueOffset),
                                   .saturation = animation.config.saturation,
                                   .value = animation.config.value,
                                   .width = animation.config.width,
                               });
    }

    void _render(const FftReactive &animation,
                 const ActiveAnimation & /*unused*/, uint32_t nowMs,
                 uint8_t hueOffset) {
        auto canvas = PrimitiveCanvas{_frame, _logicalToLocal};
        const auto baseHue =
            static_cast<uint8_t>(animation.config.baseHue + hueOffset);
        const auto snapshot = _snapshotFftFrame();
        if (!snapshot.valid) {
            drawRainbow(canvas, PrimitiveParams{
                                    .elapsedMs = nowMs,
                                    .durationMs = 2000,
                                    .hue = baseHue,
                                    .saturation = animation.config.saturation,
                                    .value = animation.config.valueScale,
                                });
            return;
        }

        for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
            const size_t band =
                (static_cast<size_t>(radial) * 8U) / Config::ringCount;
            const uint8_t bandValue =
                Render::scale8(_fftBandValue(snapshot.frame, band),
                               animation.config.valueScale);
            canvas.ring(radial,
                        HsvColor{.hue = static_cast<uint8_t>(
                                     baseHue + band * 24U),
                                 .saturation = animation.config.saturation,
                                 .value = bandValue});
        }
    }

    void _render(const PrimitiveDemo &animation, const ActiveAnimation &slot,
                 uint32_t nowMs, uint8_t hueOffset) {
        const uint32_t elapsed = nowMs - slot.startMs;
        const uint32_t duration =
            slot.lifetimeMs == 0 ? 2400U : slot.lifetimeMs;
        auto canvas = PrimitiveCanvas{_frame, _logicalToLocal};
        drawPrimitiveDemo(canvas, animation.config.primitive,
                          PrimitiveParams{
                              .elapsedMs = elapsed,
                              .durationMs = duration,
                              .hue = static_cast<uint8_t>(
                                  animation.config.hue + hueOffset),
                              .saturation = animation.config.saturation,
                              .value = animation.config.value,
                              .width = animation.config.width,
                              .density = animation.config.density,
                              .speed = animation.config.speed,
                          });
    }

    void _writeLogical(uint8_t spoke, uint8_t radial, HsvColor color,
                       BlendOp op) {
        const auto local =
            _logicalToLocal[PrimitiveCanvas::logicalIndex(spoke, radial)];
        if (local == PrimitiveCanvas::invalidLocalPixel) {
            return;
        }
        auto &dst = _frame[static_cast<size_t>(local)];
        dst = Render::blend(dst, color, op);
    }

    void _writePhysical(LedTopology::PhysicalPixelIndex physical,
                        HsvColor color, BlendOp op) {
        if constexpr (Config::ledGroupCount > 1) {
            if (!LedTopology::OwnedPixels::owns(physical)) {
                return;
            }
        }
        const auto local = LedTopology::OwnedPixels::localIndex(physical);
        auto &dst = _frame[local];
        dst = Render::blend(dst, color, op);
    }

    void _clearFrame() { _frame.fill(HsvColor{}); }

    [[nodiscard]] static uint8_t
    _rotationSpokeOffset(Angle<uint8_t> offset) {
        constexpr uint32_t angleSteps = 256U;
        const auto scaled =
            (static_cast<uint32_t>(offset.value) * Config::spokeCount) +
            (angleSteps / 2U);
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
                auto local = PrimitiveCanvas::invalidLocalPixel;
                if constexpr (Config::ledGroupCount > 1) {
                    if (LedTopology::OwnedPixels::owns(physical)) {
                        local = LedTopology::OwnedPixels::localIndex(physical);
                    }
                } else {
                    local = LedTopology::OwnedPixels::localIndex(physical);
                }
                _logicalToLocal[PrimitiveCanvas::logicalIndex(spoke, radial)] =
                    local;
            }
        }
    }

    struct FftSnapshot {
        Totem::Audio::FftFrame frame{};
        bool valid = false;
    };

    FftSnapshot _snapshotFftFrame() const {
        Totem::Mutex::ScopedSpinlockGuard guard{_inputLock};
        return FftSnapshot{.frame = _latestFftFrame, .valid = _hasFftFrame};
    }

    [[nodiscard]] static uint8_t
    _fftBandValue(const Totem::Audio::FftFrame &frame, size_t band) {
        uint16_t raw = 0;
        switch (band) {
        case 0:
            raw = frame.subBass;
            break;
        case 1:
            raw = frame.bass;
            break;
        case 2:
            raw = frame.lowMid;
            break;
        case 3:
            raw = frame.mid;
            break;
        case 4:
            raw = frame.highMid;
            break;
        case 5:
            raw = frame.presence;
            break;
        case 6:
            raw = frame.brilliance;
            break;
        default:
            raw = frame.air;
            break;
        }
        if (raw <= 255U) {
            return static_cast<uint8_t>(raw);
        }
        return static_cast<uint8_t>(std::min<uint16_t>(raw >> 8U, 255U));
    }

    template <typename T>
    static ReturnCode _encodePayload(AnimationCommand &cmd, const T &payload) {
        constexpr size_t size =
            Totem::PubSubBackend::detail::Codec<T>::encodedSize();
        static_assert(size <= LedDisplayConfig::animationCommandPayloadBytes,
                      "Animation config does not fit command payload");
        cmd.payloadSize = static_cast<uint8_t>(size);
        return Totem::PubSubBackend::detail::Codec<T>::encode(
            payload, std::span<std::byte>(cmd.payload).first(size));
    }

    template <typename T>
    static std::expected<T, ReturnCode>
    _decodePayload(const AnimationCommand &cmd) {
        constexpr size_t size =
            Totem::PubSubBackend::detail::Codec<T>::encodedSize();
        FAIL_IF(cmd.payloadSize != size,
                std::unexpected(ERR(CoreError, InvalidSize)),
                "Unexpected animation config payload size");
        return Totem::PubSubBackend::detail::Codec<T>::decode(
            std::span<const std::byte>(cmd.payload).first(size));
    }

    uint16_t _nextRequestId() {
        ++_lastRequestId;
        if (_lastRequestId == 0) {
            ++_lastRequestId;
        }
        return _lastRequestId;
    }

    void _checkDitherCadence(uint32_t nowMs, uint64_t nowUs) {
        if constexpr (!Config::temporalDithering) {
            return;
        }
        if (_lastFrameUs == 0) {
            _lastFrameUs = nowUs;
            return;
        }

        const auto elapsedUs = nowUs - _lastFrameUs;
        _lastFrameUs = nowUs;
        if (elapsedUs <= Config::frameBudgetUs) {
            return;
        }
        if (nowMs - _lastDitherErrorMs < 1000U) {
            return;
        }
        _lastDitherErrorMs = nowMs;
        _log_e("LED frame cadence below dither threshold: %llu us > %lu us",
               static_cast<unsigned long long>(elapsedUs),
               static_cast<unsigned long>(Config::frameBudgetUs));
    }

    Outputs::FastLedOutput _output;
    std::array<HsvColor, Config::ownedPixelCount> _frame{};
    std::array<LedTopology::LocalPixelIndex, Config::totalPixelCount>
        _logicalToLocal{};
    std::array<ActiveAnimation, Config::maxActiveAnimations> _animations{};
    Totem::Audio::FftFrame _latestFftFrame{};
    bool _hasFftFrame = false;
    bool _pubSubSubscribed = false;
    bool _presentStrobeEnabled = false;
    Totem::PubSubBackend::SubscriberKey _animationSub = 0;
    Totem::PubSubBackend::SubscriberKey _fftSub = 0;
    ::platform::Gpio _presentStrobeGpio;
    Totem::TaskController::RunnerKey _renderTask = 0;
    std::atomic<uint32_t> _pendingPresentStrobes{0};
    std::atomic<uint8_t> _hueOffset{0};
    std::atomic<uint8_t> _rotationOffset{0};
    uint16_t _lastRequestId = 0;
    uint64_t _lastFrameUs = 0;
    uint32_t _lastDitherErrorMs = 0;
    uint32_t _lastPresentMissLogMs = 0;
    uint32_t _presentStrobeFrames = 0;
    uint32_t _missedPresentStrobes = 0;
    uint32_t _maxShowUs = 0;
    uint32_t _frames = 0;
    mutable ::platform::Spinlock _inputLock = ::platform::create_spinlock();

    STANDARD_QUEUE(_commandQueue, AnimationCommand, Config::commandQueueSize)
};

inline constexpr LifecycleContract<Display, Config>
    _led_display_lifecycle_contract;
inline constexpr TaskControllerContract<Display>
    _led_display_task_controller_contract;
inline constexpr TaskController::TaskHooks::Contract<Display>
    _led_display_task_hooks_contract;
inline constexpr CommandsContract<Display, Commands<Display>>
    _led_display_commands_contract;

} // namespace Totem::LedDisplay::detail
