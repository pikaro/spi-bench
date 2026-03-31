#pragma once

#include "Common.hh"

#include "TaskController/detail/Config.hh"
#include "TaskController/detail/PlatformSelect.hh"
#include "TaskController/detail/Types.hh"
#include "Types/Signal.hh"

namespace Totem::TaskController::detail {

// NOTE: Convenience extraction from Loop, no domain purpose

class SignalHandler {
  public:
    struct Result {
        ReturnCode error{OK()};
        bool continueRunning = true;

        [[nodiscard]] bool ok() const { return error.ok(); }
    };

    explicit SignalHandler(TaskHooks &hooks, Config config)
        : _hooks(hooks), _name(config.name), _useNotify(config.useNotify),
          _notifyTimeoutMs(config.notifyTimeoutMs),
          _notifyExpectTimeout(config.notifyExpectTimeout) {}

    Result handleSignal() {
        auto timeout = _useNotify ? _notifyTimeoutMs : 0;
        auto waitResult =
            Platform::wait_for_signal(_name, timeout, _notifyExpectTimeout);

        if (!waitResult.ok) {
            _log_e("Runner %s: Failed to wait for signal", _name);
            return Result{
                .error = ERR(OperationFailed),
                .continueRunning = false,
            };
        }

        // Timeout was expected
        if (waitResult.timeout) {
            return {};
        }

        auto signal = waitResult.signal;
        if (signal == Signal::Stop) {
            _log_i("Runner %s received stop signal", _name);
            return Result{.continueRunning = false};
        }

        if (signal == Signal::Unknown) {
            _log_e("Runner %s received unknown signal", _name);
            return {.error = ERR(Unexpected), .continueRunning = false};
        }

        if (!_useNotify) {
            _log_e(
                "Runner %s received signal %d but notify handling is disabled",
                _name, static_cast<uint8_t>(signal));
            return {.error = ERR(Unexpected), .continueRunning = false};
        }

        auto notifyResult = _hooks.onNotify(signal);
        auto result = Result{
            .error = notifyResult,
            .continueRunning = notifyResult.ok(),
        };

        if (!notifyResult.ok()) {
            _log_e("Runner %s: onNotify hook failed for signal %d", _name,
                   static_cast<uint8_t>(signal));
        }
        return result;
    }

  private:
    TaskHooks &_hooks;
    const char *_name = "UnnamedRunner";
    bool _useNotify = false;
    uint32_t _notifyTimeoutMs = 0;
    bool _notifyExpectTimeout = true;

    using DefaultError = CoreError;
};

} // namespace Totem::TaskController::detail
