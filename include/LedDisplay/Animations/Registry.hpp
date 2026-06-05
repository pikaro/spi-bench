#pragma once

#include "LedDisplay/Animations/Bolt/Animation.hpp"
#include "LedDisplay/Animations/BreathingRings/Animation.hpp"
#include "LedDisplay/Animations/CenterWave/Animation.hpp"
#include "LedDisplay/Animations/Cymatic/Animation.hpp"
#include "LedDisplay/Animations/DiagnosticFill/Animation.hpp"
#include "LedDisplay/Animations/Lighthouse/Animation.hpp"
#include "LedDisplay/Animations/OrbitRing/Animation.hpp"
#include "LedDisplay/Animations/OrbitSparks/Animation.hpp"
#include "LedDisplay/Animations/PolarLattice/Animation.hpp"
#include "LedDisplay/Animations/RadialCurtain/Animation.hpp"
#include "LedDisplay/Animations/Shutter/Animation.hpp"
#include "LedDisplay/Animations/SineWave/Animation.hpp"
#include "LedDisplay/Animations/Sinelon/Animation.hpp"
#include "LedDisplay/Animations/SpectralIris/Animation.hpp"
#include "LedDisplay/Animations/SpectralWeave/Animation.hpp"
#include "LedDisplay/Animations/SpokeSweep/Animation.hpp"
#include "LedDisplay/Animations/StainedCells/Animation.hpp"
#include "LedDisplay/Animations/Starburst/Animation.hpp"
#include "LedDisplay/Animations/Vortex/Animation.hpp"
#include "LedDisplay/Animations/WheelIndicator/Animation.hpp"
#include "LedDisplay/Interfaces/AnimationCommandCodec.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <expected>
#include <type_traits>
#include <variant>

namespace Totem::LedDisplay::Animations {

using Payload =
    std::variant<DiagnosticFill, CenterWave, SpectralWeave, SpectralIris,
                 OrbitSparks, StainedCells, WheelIndicator, SpokeSweep, Sinelon,
                 SineWave, Starburst, Vortex, Shutter, OrbitRing, Lighthouse,
                 Cymatic, BreathingRings, RadialCurtain, PolarLattice, Bolt>;

static_assert(std::is_trivially_copyable_v<Payload>,
              "Animation payload must remain queue-copyable");

inline std::expected<Payload, ReturnCode>
makePayload(const AnimationPlayCommand &cmd) {
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
    case AnimationKind::SpectralWeave: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<SpectralWeaveConfig>(cmd),
            "Failed to decode spectral weave config");
        return Payload{SpectralWeave{.config = config}};
    }
    case AnimationKind::SpectralIris: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<SpectralIrisConfig>(cmd),
            "Failed to decode spectral iris config");
        return Payload{SpectralIris{.config = config}};
    }
    case AnimationKind::OrbitSparks: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<OrbitSparksConfig>(cmd),
            "Failed to decode orbit sparks config");
        return Payload{OrbitSparks{.config = config}};
    }
    case AnimationKind::StainedCells: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<StainedCellsConfig>(cmd),
            "Failed to decode stained cells config");
        return Payload{StainedCells{.config = config}};
    }
    case AnimationKind::Sinelon: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<SinelonConfig>(cmd),
            "Failed to decode sinelon config");
        return Payload{Sinelon{.config = config}};
    }
    case AnimationKind::SineWave: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<SineWaveConfig>(cmd),
            "Failed to decode sine wave config");
        return Payload{SineWave{.config = config}};
    }
    case AnimationKind::Starburst: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<StarburstConfig>(cmd),
            "Failed to decode starburst config");
        return Payload{Starburst{.config = config}};
    }
    case AnimationKind::Vortex: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<VortexConfig>(cmd),
            "Failed to decode vortex config");
        return Payload{Vortex{.config = config}};
    }
    case AnimationKind::Shutter: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<ShutterConfig>(cmd),
            "Failed to decode shutter config");
        return Payload{Shutter{.config = config}};
    }
    case AnimationKind::OrbitRing: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<OrbitRingConfig>(cmd),
            "Failed to decode orbit ring config");
        return Payload{OrbitRing{.config = config}};
    }
    case AnimationKind::Lighthouse: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<LighthouseConfig>(cmd),
            "Failed to decode lighthouse config");
        return Payload{Lighthouse{.config = config}};
    }
    case AnimationKind::Cymatic: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<CymaticConfig>(cmd),
            "Failed to decode cymatic config");
        return Payload{Cymatic{.config = config}};
    }
    case AnimationKind::BreathingRings: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<BreathingRingsConfig>(cmd),
            "Failed to decode breathing rings config");
        return Payload{BreathingRings{.config = config}};
    }
    case AnimationKind::RadialCurtain: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<RadialCurtainConfig>(cmd),
            "Failed to decode radial curtain config");
        return Payload{RadialCurtain{.config = config}};
    }
    case AnimationKind::PolarLattice: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            config, decodeCommandPayload<PolarLatticeConfig>(cmd),
            "Failed to decode polar lattice config");
        return Payload{PolarLattice{.config = config}};
    }
    case AnimationKind::Bolt: {
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(config,
                                          decodeCommandPayload<BoltConfig>(cmd),
                                          "Failed to decode bolt config");
        return Payload{Bolt{.config = config}};
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

inline ReturnCode update(Payload &payload, const AnimationUpdateCommand &cmd) {
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
    return std::visit([](const auto &animation) { return animation.kind; },
                      payload);
}

[[nodiscard]] inline bool requiresFullFrame(const Payload &payload) {
    return std::visit(
        [](const auto &animation) {
            return std::remove_cvref_t<decltype(animation)>::requiresFullFrame;
        },
        payload);
}

inline void render(const Payload &payload, AnimationRenderContext &ctx) {
    std::visit([&ctx](const auto &animation) { animation.render(ctx); },
               payload);
}

} // namespace Totem::LedDisplay::Animations
