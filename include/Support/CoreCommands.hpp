#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/detail/Parser.hpp"
#include "LedDisplay/Interfaces/AnimationCommandFactory.hpp"
#include "Macros/Facade.hpp"
#include "Services/Commands.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>

inline CommandDesc helloCmd = {
    .name = "hello",
    .description = "Prints Hello, World!",
    .args = {},
    .handler = [](CommandDesc::ParsedArgs /*unused*/, void *) -> ReturnCode {
        _log_i("Hello, World!");
        return OK(CoreError);
    },
    .subcommands = {},
};

namespace Totem::Support::detail {

inline std::expected<uint32_t, ReturnCode>
optionalU32(CommandDesc::ParsedArgs args, size_t index,
            uint32_t defaultValue) {
    auto parsed = args.get<uint32_t>(index);
    if (parsed.ok) {
        return parsed.value;
    }
    if (parsed.error == CommandDesc::ArgError::Missing) {
        return defaultValue;
    }
    return std::unexpected(ERR(CoreError, InvalidArgument));
}

inline uint8_t clampU8(uint32_t value) {
    return static_cast<uint8_t>(std::min<uint32_t>(value, 255U));
}

inline uint16_t clampU16(uint32_t value) {
    return static_cast<uint16_t>(std::min<uint32_t>(value, 65535U));
}

inline ReturnCode publishAnim(CommandDesc::ParsedArgs args,
                              Totem::LedDisplay::AnimationKind kind,
                              uint32_t defaultDuration,
                              uint32_t defaultHue,
                              uint32_t defaultValue,
                              uint32_t defaultParam) {
    FAIL_IF_UNEXPECTED_FWD(duration,
                           optionalU32(args, 0, defaultDuration),
                           "Invalid animation duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, optionalU32(args, 1, defaultHue),
                           "Invalid animation hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, optionalU32(args, 2, defaultValue),
                           "Invalid animation value argument");
    FAIL_IF_UNEXPECTED_FWD(param, optionalU32(args, 3, defaultParam),
                           "Invalid animation parameter argument");

    return Totem::LedDisplay::publishAnimation({
        .kind = kind,
        .lifetimeMs = clampU16(duration),
        .hue = clampU8(hue),
        .value = clampU8(value),
        .param = clampU8(param),
    });
}

inline ReturnCode handleAnimRoot(CommandDesc::ParsedArgs /*unused*/,
                                 void * /*unused*/) {
    _log_i("Use /anim wave|fill|fft|prim|stop");
    return OK();
}

inline ReturnCode handleAnimWave(CommandDesc::ParsedArgs args,
                                 void * /*unused*/) {
    return publishAnim(args, Totem::LedDisplay::AnimationKind::CenterWave,
                       1200, 144, 180, 5);
}

inline ReturnCode handleAnimFill(CommandDesc::ParsedArgs args,
                                 void * /*unused*/) {
    return publishAnim(args, Totem::LedDisplay::AnimationKind::DiagnosticFill,
                       2000, 96, 80, 0);
}

inline ReturnCode handleAnimFft(CommandDesc::ParsedArgs args,
                                void * /*unused*/) {
    return publishAnim(args, Totem::LedDisplay::AnimationKind::FftReactive, 0,
                       0, 180, 0);
}

inline ReturnCode handleAnimPrim(CommandDesc::ParsedArgs args,
                                 void * /*unused*/) {
    auto primitive = args.get<Totem::LedDisplay::PrimitiveKind>(0);
    FAIL_IF_NOT(primitive.ok, ERR(CoreError, InvalidArgument),
                "Invalid primitive kind argument");
    FAIL_IF_UNEXPECTED_FWD(duration, optionalU32(args, 1, 2400),
                           "Invalid primitive duration argument");
    FAIL_IF_UNEXPECTED_FWD(hue, optionalU32(args, 2, 144),
                           "Invalid primitive hue argument");
    FAIL_IF_UNEXPECTED_FWD(value, optionalU32(args, 3, 180),
                           "Invalid primitive value argument");
    FAIL_IF_UNEXPECTED_FWD(width, optionalU32(args, 4, 5),
                           "Invalid primitive width argument");

    return Totem::LedDisplay::publishAnimation({
        .kind = Totem::LedDisplay::AnimationKind::PrimitiveDemo,
        .primitive = primitive.value,
        .lifetimeMs = clampU16(duration),
        .hue = clampU8(hue),
        .value = clampU8(value),
        .param = clampU8(width),
    });
}

inline ReturnCode handleAnimStop(CommandDesc::ParsedArgs args,
                                 void * /*unused*/) {
    FAIL_IF_UNEXPECTED_FWD(requestId, optionalU32(args, 0, 0),
                           "Invalid animation request ID argument");
    return Totem::LedDisplay::publishAnimationCommand(
        Totem::LedDisplay::makeStopAnimationCommand(clampU16(requestId)));
}

} // namespace Totem::Support::detail

inline std::array<CommandDesc, 5> animSubcommands{{
    {
        .name = "wave",
        .description = "Publish a center wave animation",
        .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                     "durationMs", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "hue", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "value", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "width", CommandDesc::ArgRequirement::Optional)},
        .handler = Totem::Support::detail::handleAnimWave,
        .subcommands = {},
    },
    {
        .name = "fill",
        .description = "Publish a diagnostic fill animation",
        .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                     "durationMs", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "hue", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "value", CommandDesc::ArgRequirement::Optional)},
        .handler = Totem::Support::detail::handleAnimFill,
        .subcommands = {},
    },
    {
        .name = "fft",
        .description = "Publish the persistent FFT reactive animation",
        .args = {Totem::CommandBackend::detail::arg<uint32_t>(
                     "durationMs", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "hue", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "valueScale", CommandDesc::ArgRequirement::Optional)},
        .handler = Totem::Support::detail::handleAnimFft,
        .subcommands = {},
    },
    {
        .name = "prim",
        .description = "Publish a primitive demo animation",
        .args = {Totem::CommandBackend::detail::arg<
                     Totem::LedDisplay::PrimitiveKind>("primitive"),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "durationMs", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "hue", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "value", CommandDesc::ArgRequirement::Optional),
                 Totem::CommandBackend::detail::arg<uint32_t>(
                     "width", CommandDesc::ArgRequirement::Optional)},
        .handler = Totem::Support::detail::handleAnimPrim,
        .subcommands = {},
    },
    {
        .name = "stop",
        .description = "Publish an animation stop command",
        .args = {Totem::CommandBackend::detail::arg<uint32_t>(
            "requestId", CommandDesc::ArgRequirement::Optional)},
        .handler = Totem::Support::detail::handleAnimStop,
        .subcommands = {},
    },
}};

inline CommandDesc animCmd = {
    .name = "anim",
    .description = "Publish LED animation commands over PubSub",
    .args = {},
    .handler = Totem::Support::detail::handleAnimRoot,
    .subcommands = animSubcommands,
};

inline ReturnCode register_core_commands() {
    auto &reg = CommandRegistrarService::get();

    FAIL_IF_UNEXPECTED_FWD(helloKey, reg.registerCommand(helloCmd),
                           "Failed to register hello command");
    (void)helloKey;
    FAIL_IF_UNEXPECTED_FWD(animKey, reg.registerCommand(animCmd),
                           "Failed to register anim command");
    (void)animKey;

    return OK(CoreError);
}
