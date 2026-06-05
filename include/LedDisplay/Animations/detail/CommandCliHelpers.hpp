#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "LedDisplay/Interfaces/AnimationCommandFactory.hpp"
#include "Types/Angle.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <type_traits>

namespace Totem::LedDisplay::Animations::detail {

inline bool isOptionalDefaultMarker(CommandDesc::ParsedArgs args,
                                    size_t index) {
    if (index >= args.tokens.size() || args.tokens[index] != "-") {
        return false;
    }
    if (index >= args.desc.args.size()) {
        return false;
    }

    const auto &arg = args.desc.args[index];
    return !arg.name.empty() &&
           arg.requirement == CommandDesc::ArgRequirement::Optional;
}

inline std::expected<uint32_t, ReturnCode>
optionalU32(CommandDesc::ParsedArgs args, size_t index, uint32_t defaultValue) {
    if (isOptionalDefaultMarker(args, index)) {
        return defaultValue;
    }

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
    return static_cast<uint8_t>(
        std::min<uint32_t>(value, std::numeric_limits<uint8_t>::max()));
}

inline uint16_t clampU16(uint32_t value) {
    return static_cast<uint16_t>(
        std::min<uint32_t>(value, std::numeric_limits<uint16_t>::max()));
}

inline Angle<uint8_t> rawAngleU8(uint32_t value) {
    return Angle<uint8_t>::fromRaw(clampU8(value));
}

inline Angle<uint8_t> degreesAngleU8(uint32_t degrees) {
    constexpr uint32_t degreesPerTurn = 360U;
    return Angle<uint8_t>::fromDeg(
        static_cast<float>(degrees % degreesPerTurn));
}

template <typename Command>
inline ReturnCode publishCommand(std::expected<Command, ReturnCode> result) {
    FAIL_IF_UNEXPECTED_FWD(cmd, result, "Failed to build animation command");
    if constexpr (std::is_same_v<Command,
                                 Totem::LedDisplay::AnimationPlayCommand>) {
        return Totem::LedDisplay::publishAnimationPlayCommand(cmd);
    } else if constexpr (std::is_same_v<
                             Command,
                             Totem::LedDisplay::AnimationUpdateCommand>) {
        return Totem::LedDisplay::publishAnimationUpdateCommand(cmd);
    } else if constexpr (
        std::is_same_v<Command, Totem::LedDisplay::AnimationStopCommand> ||
        std::is_same_v<Command,
                       Totem::LedDisplay::AnimationSetHueOffsetCommand> ||
        std::is_same_v<Command,
                       Totem::LedDisplay::AnimationSetRotationOffsetCommand> ||
        std::is_same_v<Command,
                       Totem::LedDisplay::AnimationSetBrightnessCommand> ||
        std::is_same_v<Command,
                       Totem::LedDisplay::AnimationSetLayerActiveCommand> ||
        std::is_same_v<Command,
                       Totem::LedDisplay::AnimationSetLayerOpacityCommand> ||
        std::is_same_v<Command,
                       Totem::LedDisplay::AnimationFadeLayerSwapCommand>) {
        return Totem::LedDisplay::publishAnimationCommand(cmd);
    } else {
        static_assert(std::is_same_v<Command, void>,
                      "Unsupported LED animation command type");
        return ERR(CoreError, InvalidArgument);
    }
}

} // namespace Totem::LedDisplay::Animations::detail
