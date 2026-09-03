#include "src/master/debug_mode.hpp"
#include <iostream>

namespace {

int failures = 0;
int startCalls = 0;
int stopCalls = 0;
bool failStart = false;
bool failStop = false;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

ReturnCode onStart() {
    ++startCalls;
    return ReturnCode::from(failStart ? CoreError::OperationFailed
                                      : CoreError::Ok);
}

ReturnCode onStop() {
    ++stopCalls;
    return ReturnCode::from(failStop ? CoreError::OperationFailed
                                     : CoreError::Ok);
}

void testSuccessfulTransitionsAreIdempotent() {
    MasterDebugMode::Mode<onStart, onStop> mode{};

    expect(!mode.active(), "debug mode must start inactive");
    expect(mode.start().ok(), "debug mode start must succeed");
    expect(mode.active(), "successful start must activate debug mode");
    expect(startCalls == 1, "start hook must run once");

    expect(mode.start().ok(), "repeated debug mode start must be idempotent");
    expect(startCalls == 1, "idempotent start must not rerun the hook");

    expect(mode.toggle().ok(), "toggle must stop an active debug mode");
    expect(!mode.active(), "successful stop must deactivate debug mode");
    expect(stopCalls == 1, "stop hook must run once");

    expect(mode.stop().ok(), "repeated debug mode stop must be idempotent");
    expect(stopCalls == 1, "idempotent stop must not rerun the hook");
}

void testFailedHooksDoNotChangeState() {
    MasterDebugMode::Mode<onStart, onStop> mode{};

    failStart = true;
    expect(!mode.start().ok(), "failed start hook must surface its error");
    expect(!mode.active(), "failed start hook must leave debug mode inactive");
    failStart = false;

    expect(mode.start().ok(), "debug mode must recover after a start failure");
    failStop = true;
    expect(!mode.stop().ok(), "failed stop hook must surface its error");
    expect(mode.active(), "failed stop hook must leave debug mode active");
    failStop = false;

    expect(mode.stop().ok(), "debug mode must recover after a stop failure");
    expect(!mode.active(), "recovered stop must deactivate debug mode");
}

} // namespace

int main() {
    testSuccessfulTransitionsAreIdempotent();
    testFailedHooksDoNotChangeState();

    if (failures != 0) {
        std::cerr << failures << " master debug-mode test(s) failed\n";
        return 1;
    }
    std::cout << "Master debug-mode tests passed\n";
    return 0;
}
