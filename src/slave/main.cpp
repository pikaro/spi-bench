#include "Macros/Facade.hpp"
#include "Setups/Core.hpp"
#include "Setups/PubSubTest.hpp"

#include "Clock/Facade.hpp"
#include "Wire/Rs485/Facade.hpp"
#include "Wire/Spi/Facade.hpp"

CoreSetup core{};
PubSubTestSetup pubSubTestSetup{core.taskRegistry};

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
