#pragma once

#include "Base/HasCommands.hpp"
#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Interfaces/Types.hpp"
#include "LedDisplay/Outputs/FastLedOutput.hpp"
#include "LedDisplay/detail/Commands.hpp"
#include "LedDisplay/detail/RendererSelect.hpp"
#include "LedTopology/Facade.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/detail/Codec.hpp"
#include "Queue/Facade.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <array>
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
        AnimationKind kind = AnimationKind::DiagnosticFill;
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

  private:
    ReturnCode _onBegin() {
        static_assert(Config::dataLineCount <= 2,
                      "FastLED output currently supports two configured lines");
        DEFAULT_TASK();
        INIT_QUEUE_OR_FAIL(_commandQueue);
        FAIL_IF_ERR_FWD(_output.begin(config()),
                        "Failed to initialize LED output backend");
        FAIL_IF_ERR_FWD(this->_registerCommands(),
                        "Failed to register LedDisplay commands");
        START_TASK();
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        ret.combine(this->_endTaskController());
        ret.combine(this->_deregisterCommands());
        ret.combine(_output.deinit());
        DESTROY_QUEUE(ret, _commandQueue);
        return ret;
    }

    ReturnCode _onTaskStep() {
        const auto nowMs = ::platform::get_time();
        const auto nowUs = ::platform::get_time_us();
        _checkDitherCadence(nowMs, nowUs);

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
                FAIL_ERR_FWD(result,
                             "Failed to receive LED animation command");
            }
            FAIL_IF_ERR_FWD(_handleCommand(cmd, nowMs),
                            "Failed to handle LED animation command");
        }
    }

    ReturnCode _handleCommand(const AnimationCommand &cmd, uint32_t nowMs) {
        FAIL_IF_NOT(cmd.validate(), ERR(CoreError, InvalidArgument),
                    "Invalid LED animation command");
        switch (cmd.type) {
        case AnimationCommandType::Play:
            return _play(cmd, nowMs);
        case AnimationCommandType::Stop:
            _stop(cmd.requestId);
            return OK();
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

    void _renderAnimations(uint32_t nowMs) {
        for (auto &slot : _animations) {
            if (!slot.active) {
                continue;
            }
            if (_expired(slot, nowMs)) {
                slot.active = false;
                ++slot.generation;
                continue;
            }
            std::visit(
                [this, &slot, nowMs](const auto &animation) {
                    _render(animation, slot, nowMs);
                },
                slot.payload);
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
                 const ActiveAnimation & /*unused*/, uint32_t /*unused*/) {
        const auto color = HsvColor{.hue = animation.config.hue,
                                    .saturation = animation.config.saturation,
                                    .value = animation.config.value};
        for (auto &pixel : _frame) {
            pixel = color;
        }
    }

    void _render(const CenterWave &animation, const ActiveAnimation &slot,
                 uint32_t nowMs) {
        const uint32_t elapsed = nowMs - slot.startMs;
        const uint32_t duration = slot.lifetimeMs == 0 ? 1200U : slot.lifetimeMs;
        const uint32_t width = std::max<uint32_t>(animation.config.width, 1U);
        const uint32_t extent = Config::ringCount + width + 1U;
        const uint32_t head = (elapsed * extent) / duration;

        for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
            for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
                const auto diff =
                    head > radial ? head - radial : radial - head;
                if (diff > width) {
                    continue;
                }
                const auto scale =
                    static_cast<uint8_t>(((width + 1U - diff) * 255U) /
                                         (width + 1U));
                const auto value = Render::scale8(animation.config.value, scale);
                _writeLogical(spoke, radial,
                              HsvColor{.hue = animation.config.hue,
                                       .saturation =
                                           animation.config.saturation,
                                       .value = value},
                              BlendOp::MaxValue);
            }
        }
    }

    void _render(const FftReactive &animation,
                 const ActiveAnimation & /*unused*/, uint32_t nowMs) {
        const auto pulse =
            static_cast<uint8_t>((nowMs / 8U) & 0xFFU);
        for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
            for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
                const uint8_t wave =
                    static_cast<uint8_t>((pulse + radial * 5U + spoke * 7U) &
                                         0xFFU);
                const uint8_t value =
                    Render::scale8(wave, animation.config.valueScale);
                _writeLogical(spoke, radial,
                              HsvColor{.hue = static_cast<uint8_t>(
                                           animation.config.baseHue + spoke * 8U),
                                       .saturation =
                                           animation.config.saturation,
                                       .value = value},
                              BlendOp::MaxValue);
            }
        }
    }

    void _writeLogical(uint8_t spoke, uint8_t radial, HsvColor color,
                       BlendOp op) {
        const auto physical = LedTopology::Umbrella::physicalFor(spoke, radial);
        _writePhysical(physical, color, op);
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

    template <typename T>
    static ReturnCode _encodePayload(AnimationCommand &cmd, const T &payload) {
        constexpr size_t size = Totem::PubSubBackend::detail::Codec<
            T>::encodedSize();
        static_assert(size <= LedDisplayConfig::animationCommandPayloadBytes,
                      "Animation config does not fit command payload");
        cmd.payloadSize = static_cast<uint8_t>(size);
        return Totem::PubSubBackend::detail::Codec<T>::encode(
            payload, std::span<std::byte>(cmd.payload).first(size));
    }

    template <typename T>
    static std::expected<T, ReturnCode>
    _decodePayload(const AnimationCommand &cmd) {
        constexpr size_t size = Totem::PubSubBackend::detail::Codec<
            T>::encodedSize();
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
    std::array<ActiveAnimation, Config::maxActiveAnimations> _animations{};
    uint16_t _lastRequestId = 0;
    uint64_t _lastFrameUs = 0;
    uint32_t _lastDitherErrorMs = 0;
    uint32_t _maxShowUs = 0;
    uint32_t _frames = 0;

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
