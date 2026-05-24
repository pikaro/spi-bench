#pragma once

#include "LedDisplay/Animations/CenterWave.hpp"
#include "LedDisplay/Animations/DiagnosticFill.hpp"
#include "LedDisplay/Animations/FftReactive.hpp"
#include "LedDisplay/Interfaces/AnimationCommandCodec.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <expected>
#include <type_traits>
#include <variant>

namespace Totem::LedDisplay::Animations {

using Payload = std::variant<DiagnosticFill, CenterWave, FftReactive>;

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
    case AnimationKind::None:
    default:
        FAIL(std::unexpected(ERR(CoreError, InvalidArgument)),
             "Unknown animation kind");
    }
}

[[nodiscard]] inline AnimationStyle style(const Payload &payload) {
    return std::visit(
        [](const auto &animation) { return animation.defaultStyle; }, payload);
}

inline void render(const Payload &payload, AnimationRenderContext &ctx) {
    std::visit([&ctx](const auto &animation) { animation.render(ctx); },
               payload);
}

} // namespace Totem::LedDisplay::Animations
