#pragma once

#include "Macros/internal/Markers.hpp"
#include <type_traits>

namespace Totem::LedDisplay {

/** Announces that a GPU has initialized its local LED output. */
struct WIRE_MSG OutputReadyEvent {
    bool ready = true;

    [[nodiscard]] constexpr bool validate() const { return ready; }
};

/** Controls the shared hardware gate for the GPU LED signal paths. */
struct WIRE_MSG OutputEnableCommand {
    bool enabled = true;

    [[nodiscard]] constexpr bool validate() const { return true; }
};

static_assert(std::is_trivially_copyable_v<OutputReadyEvent>);
static_assert(std::is_trivially_copyable_v<OutputEnableCommand>);

} // namespace Totem::LedDisplay
