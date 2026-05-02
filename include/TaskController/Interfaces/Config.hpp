#pragma once

#include <cstdint>

namespace Totem::TaskController {

struct Config {
    struct CorePreference {
        enum class Kind : uint8_t { Any, Specific };
        Kind kind = Kind::Any;
        uint8_t core = 0;

        static constexpr CorePreference any() { return {}; }
        static constexpr CorePreference specific(uint8_t core) {
            return CorePreference{.kind = Kind::Specific, .core = core};
        }
    };

    const char *name = "UNDEFINED";
    uint8_t priority = 1;
    CorePreference core = CorePreference::any();
    uint32_t stackSize = 4096;
    uint32_t intervalMs = 1000;
    bool noCatchup = false;
    bool useWdt = true;
    bool useNotify = false;
    bool notifyExpectTimeout = true;
    uint32_t notifyTimeoutMs = 1000;
    uint32_t endTimeoutMs = 5000;
    bool autoRestart = true;

    [[nodiscard]] bool validate() const {
        return name != nullptr && priority > 0 && stackSize > 0 &&
               intervalMs > 0 && notifyTimeoutMs > 0 && endTimeoutMs > 0;
    }
};

} // namespace Totem::TaskController
