#include "Macros/Facade.hpp"
#include "Setups/Core.hpp"
#include "Setups/PubSubTest.hpp"

CoreSetup core{};
PubSubTestSetup pubSubTestSetup{core.taskRegistry};

// NOLINTNEXTLINE(readability-function-size)
void setup() {
    core.setup();
    pubSubTestSetup.setup();
    _log_i("Setup complete");
}

extern "C" {
void app_main(void);
}

void app_main() {
    setup();
    for (;;) {
        const auto nowMs = ::platform::get_time();
        (void)core.work(nowMs);
        (void)pubSubTestSetup.work(nowMs);
        ::platform::delay(::platform::ms_to_ticks(publishIntervalMs));
    }
}
