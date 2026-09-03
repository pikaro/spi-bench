#pragma once

#include "LedDisplay/Animations/AnimationStopCommandDesc.hpp"
#include "LedDisplay/Animations/Bolt/CommandDesc.hpp"
#include "LedDisplay/Animations/BreathingRings/CommandDesc.hpp"
#include "LedDisplay/Animations/CenterWave/CommandDesc.hpp"
#include "LedDisplay/Animations/Cymatic/CommandDesc.hpp"
#include "LedDisplay/Animations/DiagnosticFill/CommandDesc.hpp"
#include "LedDisplay/Animations/Lighthouse/CommandDesc.hpp"
#include "LedDisplay/Animations/OrbitRing/CommandDesc.hpp"
#include "LedDisplay/Animations/OrbitSparks/CommandDesc.hpp"
#include "LedDisplay/Animations/PolarLattice/CommandDesc.hpp"
#include "LedDisplay/Animations/RadialCurtain/CommandDesc.hpp"
#include "LedDisplay/Animations/RadialGauge/CommandDesc.hpp"
#include "LedDisplay/Animations/RadialMenu/CommandDesc.hpp"
#include "LedDisplay/Animations/Shutter/CommandDesc.hpp"
#include "LedDisplay/Animations/SineWave/CommandDesc.hpp"
#include "LedDisplay/Animations/Sinelon/CommandDesc.hpp"
#include "LedDisplay/Animations/SpectralIris/CommandDesc.hpp"
#include "LedDisplay/Animations/SpectralWeave/CommandDesc.hpp"
#include "LedDisplay/Animations/SpokeSweep/CommandDesc.hpp"
#include "LedDisplay/Animations/StainedCells/CommandDesc.hpp"
#include "LedDisplay/Animations/Starburst/CommandDesc.hpp"
#include "LedDisplay/Animations/Vortex/CommandDesc.hpp"
#include "LedDisplay/Animations/WheelIndicator/CommandDesc.hpp"
#include "LedDisplay/Commands/DisplayCommandDesc.hpp"
#include "LedDisplay/Commands/LayerCommandDesc.hpp"
#include "Services/Commands.hpp"

namespace Totem::Support::detail {

inline ReturnCode handleAnimRoot(CommandDesc::ParsedArgs /*unused*/,
                                 void * /*unused*/) {
    _log_i("Use /anim "
           "wave|starburst|vortex|shutter|orbit|lighthouse|cymatic|rings|"
           "curtain|lattice|bolt|sinelon|sine|fill|weave|iris|sparks|cells|"
           "sweep|wheel|wheel-update|gauge|menu|stop");
    return OK();
}

inline ReturnCode handleDispRoot(CommandDesc::ParsedArgs /*unused*/,
                                 void * /*unused*/) {
    _log_i("Use /disp hue|rot|brightness");
    return OK();
}

inline ReturnCode handleLayerRoot(CommandDesc::ParsedArgs /*unused*/,
                                  void * /*unused*/) {
    _log_i("Use /layer active|opacity|swap");
    return OK();
}

} // namespace Totem::Support::detail

inline constinit std::array<CommandDesc, 24> animSubcommands{{
    Totem::LedDisplay::Animations::centerWaveSubcommand,
    Totem::LedDisplay::Animations::starburstSubcommand,
    Totem::LedDisplay::Animations::vortexSubcommand,
    Totem::LedDisplay::Animations::shutterSubcommand,
    Totem::LedDisplay::Animations::orbitRingSubcommand,
    Totem::LedDisplay::Animations::lighthouseSubcommand,
    Totem::LedDisplay::Animations::cymaticSubcommand,
    Totem::LedDisplay::Animations::breathingRingsSubcommand,
    Totem::LedDisplay::Animations::radialCurtainSubcommand,
    Totem::LedDisplay::Animations::polarLatticeSubcommand,
    Totem::LedDisplay::Animations::boltSubcommand,
    Totem::LedDisplay::Animations::diagnosticFillSubcommand,
    Totem::LedDisplay::Animations::sinelonSubcommand,
    Totem::LedDisplay::Animations::sineWaveSubcommand,
    Totem::LedDisplay::Animations::spectralWeaveSubcommand,
    Totem::LedDisplay::Animations::spectralIrisSubcommand,
    Totem::LedDisplay::Animations::orbitSparksSubcommand,
    Totem::LedDisplay::Animations::stainedCellsSubcommand,
    Totem::LedDisplay::Animations::spokeSweepSubcommand,
    Totem::LedDisplay::Animations::wheelIndicatorSubcommand,
    Totem::LedDisplay::Animations::wheelIndicatorUpdateSubcommand,
    Totem::LedDisplay::Animations::radialGaugeSubcommand,
    Totem::LedDisplay::Animations::radialMenuSubcommand,
    Totem::LedDisplay::Animations::animationStopSubcommand,
}};

inline constinit CommandDesc animCmd = {
    .name = "anim",
    .description = "Publish LED display commands over PubSub",
    .args = {},
    .handler = Totem::Support::detail::handleAnimRoot,
    .subcommands = animSubcommands,
};

inline constinit std::array<CommandDesc, 3> dispSubcommands{{
    Totem::LedDisplay::Commands::hueOffsetSubcommand,
    Totem::LedDisplay::Commands::rotationOffsetSubcommand,
    Totem::LedDisplay::Commands::brightnessSubcommand,
}};

inline constinit CommandDesc dispCmd = {
    .name = "disp",
    .description = "Publish LED display controls over PubSub",
    .args = {},
    .handler = Totem::Support::detail::handleDispRoot,
    .subcommands = dispSubcommands,
};

inline constinit std::array<CommandDesc, 3> layerSubcommands{{
    Totem::LedDisplay::Commands::layerActiveSubcommand,
    Totem::LedDisplay::Commands::layerOpacitySubcommand,
    Totem::LedDisplay::Commands::layerSwapSubcommand,
}};

inline constinit CommandDesc layerCmd = {
    .name = "layer",
    .description = "Publish LED layer controls over PubSub",
    .args = {},
    .handler = Totem::Support::detail::handleLayerRoot,
    .subcommands = layerSubcommands,
};

inline ReturnCode register_display_commands() {
    auto &reg = CommandRegistrarService::get();

    FAIL_IF_UNEXPECTED_FWD(animKey, reg.registerCommand(animCmd),
                           "Failed to register anim command");
    (void)animKey;
    FAIL_IF_UNEXPECTED_FWD(dispKey, reg.registerCommand(dispCmd),
                           "Failed to register disp command");
    (void)dispKey;
    FAIL_IF_UNEXPECTED_FWD(layerKey, reg.registerCommand(layerCmd),
                           "Failed to register layer command");
    (void)layerKey;

    return OK(CoreError);
}
