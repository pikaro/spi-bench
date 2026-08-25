// IWYU pragma: private

#pragma once

#include "Base/HasLifecycle.hpp"
#include "Button/Interfaces/Config.hpp"
#include "Button/Interfaces/Types.hpp"
#include "Button/detail/Metrics.hpp"
#include "Button/detail/Types.hpp"
#include "DigitalInput/Facade.hpp"
#include "Generic/InlineCallback.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <atomic>
#include <cstdint>
#include <utility>

namespace Totem::Button::detail {

class Button : public HasLifecycle<Button, Config> {
    using Self = Button;

    friend class HasLifecycle<Self, Config>;
    friend struct LifecycleContract<Self, Config>;

  public:
    template <typename Callback>
    explicit Button(Callback callback)
        : _callback(std::move(callback)),
          _input([this](DigitalInput::Event event) { _handleInput(event); }) {}

    DELETE_COPY(Button)
    DELETE_MOVE(Button)

    static constexpr const char *name = "Button";

    ReturnCode work(uint32_t nowMs) {
        FAIL_IF_NOT(this->active(), ERR(InvalidState),
                    "Cannot work inactive button %s",
                    this->config().input.name);
        auto ret = _input.work(nowMs);
        if (static_cast<uint32_t>(nowMs - _lastMetricsFlushMs) >=
            metricsFlushIntervalMs) {
            _lastMetricsFlushMs = nowMs;
            _flushMetrics();
        }
        return ret;
    }

  private:
    ReturnCode _onBegin() {
        prewarmMetrics();
        FAIL_IF_ERR_FWD(_input.begin(this->config().input),
                        "Failed to begin digital input for button %s",
                        this->config().input.name);
        _log_i("Configured button %s as active-%s", this->config().input.name,
               this->config().activeLow ? "low" : "high");
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = _input.end();
        _flushMetrics();
        return ret;
    }

    void _handleInput(DigitalInput::Event event) {
        const bool pressed = event.level != this->config().activeLow;
        if ((pressed && !this->config().notifyPressed) ||
            (!pressed && !this->config().notifyReleased)) {
            _ignored.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        _events.fetch_add(1, std::memory_order_relaxed);
        _callback(pressed ? Event::Pressed : Event::Released);
    }

    void _flushMetrics() {
        const auto events = _events.exchange(0, std::memory_order_relaxed);
        if (events != 0) {
            metrics().addEvents(events);
        }
        const auto ignored = _ignored.exchange(0, std::memory_order_relaxed);
        if (ignored != 0) {
            metrics().addIgnored(ignored);
        }
    }

    Generic::InlineCallback<Event> _callback;
    DigitalInput::DigitalInput _input;
    std::atomic<uint32_t> _events{0};
    std::atomic<uint32_t> _ignored{0};
    uint32_t _lastMetricsFlushMs = 0;

    static constexpr uint32_t metricsFlushIntervalMs = 100U;

    static constexpr LogComponent logComponent =
        Totem::Button::detail::logComponent;
};

inline constexpr LifecycleContract<Button, Config> _button_lifecycle_contract;

} // namespace Totem::Button::detail
