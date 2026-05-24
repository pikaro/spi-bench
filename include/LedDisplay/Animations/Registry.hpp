#pragma once

#include "LedDisplay/Animations/CenterWave.hpp"
#include "LedDisplay/Animations/DiagnosticFill.hpp"
#include "LedDisplay/Animations/FftReactive.hpp"
#include "LedDisplay/Animations/SpokeSweep.hpp"
#include "LedDisplay/Animations/WheelIndicator.hpp"
#include "LedDisplay/Interfaces/AnimationCommandCodec.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <expected>
#include <type_traits>
#include <variant>

namespace Totem::LedDisplay::Animations {

using Payload =
    std::variant<DiagnosticFill, CenterWave, FftReactive, WheelIndicator,
                 SpokeSweep>;

static_assert(std::is_trivially_copyable_v<Payload>,
              "Animation payload must remain queue-copyable");

inline std::expected<Payload, ReturnCode>
makePayload(const AnimationCommand &cmd) {
    switch (cmd.kind) {
    case AnimationKind::DiagnosticFill: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<DiagnosticFillConfig>(cmd),
            "Failed to decode diagnostic fill config");
        return Payload{DiagnosticFill{.config = config}};
    }
    case AnimationKind::CenterWave: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<CenterWaveConfig>(cmd),
            "Failed to decode center wave config");
        return Payload{CenterWave{.config = config}};
    }
    case AnimationKind::FftReactive: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<FftReactiveConfig>(cmd),
            "Failed to decode FFT reactive config");
        return Payload{FftReactive{.config = config}};
    }
    case AnimationKind::WheelIndicator: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<WheelIndicatorConfig>(cmd),
            "Failed to decode wheel indicator config");
        return Payload{WheelIndicator{.config = config}};
    }
    case AnimationKind::SpokeSweep: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<SpokeSweepConfig>(cmd),
            "Failed to decode spoke sweep config");
        return Payload{SpokeSweep{.config = config}};
    }
    case AnimationKind::None:
    default:
        FAIL(std::unexpected(ERR(CoreError, InvalidArgument)),
             "Unknown animation kind");
    }
}

inline ReturnCode update(Payload &payload, const AnimationCommand &cmd) {
    return std::visit(
        [&cmd](auto &animation) -> ReturnCode {
            if (cmd.kind != animation.kind) {
                FAIL(ERR(CoreError, InvalidArgument),
                     "Animation update kind does not match active payload");
            }
            using Config = std::remove_cvref_t<decltype(animation.config)>;
            FAIL_IF_UNEXPECTED_FWD(config, decodeCommandPayload<Config>(cmd),
                                   "Failed to decode animation update config");
            animation.config = config;
            return OK();
        },
        payload);
}

[[nodiscard]] inline AnimationStyle style(const Payload &payload) {
    return std::visit(
        [](const auto &animation) { return animation.defaultStyle; }, payload);
}

[[nodiscard]] inline AnimationKind kind(const Payload &payload) {
    return std::visit(
        [](const auto &animation) { return animation.kind; }, payload);
}

inline void render(const Payload &payload, AnimationRenderContext &ctx) {
    std::visit([&ctx](const auto &animation) { animation.render(ctx); },
               payload);
}

} // namespace Totem::LedDisplay::Animations
