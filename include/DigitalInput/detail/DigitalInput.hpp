// IWYU pragma: private

#pragma once

#include "Base/HasLifecycle.hpp"
#include "DigitalInput/Interfaces/Config.hpp"
#include "DigitalInput/Interfaces/Types.hpp"
#include "DigitalInput/detail/Metrics.hpp"
#include "DigitalInput/detail/Types.hpp"
#include "Generic/InlineCallback.hpp"
#include "Macros/Facade.hpp"
#include "Platform/Gpio.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Types/Error.hpp"
#include "Types/Gpio.hpp"
#include <atomic>
#include <cstdint>
#include <expected>
#include <utility>

namespace Totem::DigitalInput::detail {

class DigitalInput : public HasLifecycle<DigitalInput, Config> {
    using Self = DigitalInput;

    friend class HasLifecycle<Self, Config>;
    friend struct LifecycleContract<Self, Config>;

  public:
    template <typename Callback>
    explicit DigitalInput(Callback callback) : _callback(std::move(callback)) {}

    DELETE_COPY(DigitalInput)
    DELETE_MOVE(DigitalInput)

    static constexpr const char *name = "DigitalInput";

    ReturnCode work(uint32_t nowMs) {
        FAIL_IF_NOT(this->active(), ERR(InvalidState),
                    "Cannot work inactive digital input %s",
                    this->config().name);

        if (this->config().pollIntervalMs.has_value() &&
            static_cast<uint32_t>(nowMs - _lastPollMs) >=
                *this->config().pollIntervalMs) {
            _lastPollMs = nowMs;
            FAIL_IF_ERR_FWD(_poll(), "Failed to poll digital input %s",
                            this->config().name);
        }

        if (this->config().debounceMs.has_value()) {
            _workDebounce(nowMs);
        }
        if (static_cast<uint32_t>(nowMs - _lastMetricsFlushMs) >=
            metricsFlushIntervalMs) {
            _lastMetricsFlushMs = nowMs;
            _flushMetrics();
        }
        return OK();
    }

    /** Read the current electrical GPIO level without applying debounce. */
    [[nodiscard]] std::expected<bool, ReturnCode> level() const {
        FAIL_IF_NOT(
            this->active(), std::unexpected(ERR(CoreError, InvalidState)),
            "Cannot read inactive digital input %s", this->config().name);
        return _gpio.level();
    }

  private:
    static constexpr uint32_t levelMask = 1U;
    static constexpr uint32_t sourceMask = 2U;
    static constexpr uint32_t generationIncrement = 4U;
    static constexpr uint32_t metricsFlushIntervalMs = 100U;

    ReturnCode _onBegin() {
        prewarmMetrics();

        FAIL_IF_ERR_FWD(_gpio.initInput(this->config().pin, this->config().pull,
                                        GpioInterrupt::AnyEdge),
                        "Failed to initialize GPIO for digital input %s",
                        this->config().name);
        FAIL_IF_UNEXPECTED_FWD(initialLevel, _gpio.level(),
                               "Failed to read initial GPIO level for digital "
                               "input %s",
                               this->config().name);

        const uint32_t initialState = initialLevel ? levelMask : 0U;
        _rawState.store(initialState, std::memory_order_relaxed);
        _candidateState = initialState;
        _acceptedLevel = initialLevel;
        _lastPollMs = ::platform::get_time();
        _lastMetricsFlushMs = _lastPollMs;

        FAIL_IF_ERR_FWD(_gpio.registerIsr(this, _isrCallback),
                        "Failed to set ISR callback for digital input %s",
                        this->config().name);
        _log_i("Configured digital input %s on pin " SV_FMT " with pull " SV_FMT
               "; debounce=%u ms, poll=%u ms, initial "
               "level=%u",
               this->config().name, MAGIC_SV_ARG(this->config().pin),
               MAGIC_SV_ARG(this->config().pull),
               this->config().debounceMs.value_or(0),
               this->config().pollIntervalMs.value_or(0), initialLevel ? 1 : 0);
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = _gpio.deinit();
        _flushMetrics();
        _rawState.store(0, std::memory_order_relaxed);
        _candidateState = 0;
        _acceptedLevel = false;
        return ret;
    }

    ReturnCode _poll() {
        const uint32_t observedState =
            _rawState.load(std::memory_order_acquire);
        const auto timestampUs = ::platform::get_time_us();
        FAIL_IF_UNEXPECTED_FWD(level, _gpio.level(),
                               "Failed to read GPIO for digital input %s",
                               this->config().name);
        if (_level(observedState) == level) {
            return OK();
        }

        uint32_t expected = observedState;
        const uint32_t next =
            _nextState(observedState, level, EventSource::Poll);
        if (!_rawState.compare_exchange_strong(expected, next,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
            _duplicates.fetch_add(1, std::memory_order_relaxed);
            return OK();
        }

        _pollChanges.fetch_add(1, std::memory_order_relaxed);
        _log_v("Polled digital input %s level change on pin " SV_FMT
               ": level=%u",
               this->config().name, MAGIC_SV_ARG(this->config().pin),
               level ? 1 : 0);
        if (!this->config().debounceMs.has_value()) {
            _emit(level, EventSource::Poll, timestampUs);
        }
        return OK();
    }

    static void _isrCallback(void *owner, GpioEvent event) {
        auto *self = static_cast<Self *>(owner);
        if (self == nullptr) {
            return;
        }
        self->_handleIsr(event);
    }

    void _handleIsr(GpioEvent event) {
        _isrEvents.fetch_add(1, std::memory_order_relaxed);

        uint32_t current = _rawState.load(std::memory_order_acquire);
        for (;;) {
            if (_level(current) == event.level) {
                _duplicates.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            const uint32_t next =
                _nextState(current, event.level, EventSource::Interrupt);
            if (_rawState.compare_exchange_weak(current, next,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
                break;
            }
        }

        if (!this->config().debounceMs.has_value()) {
            _emit(event.level, EventSource::Interrupt, event.timestampUs);
        }
    }

    void _workDebounce(uint32_t nowMs) {
        const uint32_t rawState = _rawState.load(std::memory_order_acquire);
        if (rawState != _candidateState) {
            _candidateState = rawState;
            _candidateSinceMs = nowMs;
            return;
        }
        if (_level(rawState) == _acceptedLevel ||
            static_cast<uint32_t>(nowMs - _candidateSinceMs) <
                *this->config().debounceMs) {
            return;
        }

        // An ISR may supersede the candidate after the first load. Only emit
        // a level that remained current through the complete stability check.
        if (_rawState.load(std::memory_order_acquire) != rawState) {
            return;
        }

        _acceptedLevel = _level(rawState);
        _debouncedEvents.fetch_add(1, std::memory_order_relaxed);
        _emit(_acceptedLevel, _source(rawState), ::platform::get_time_us());
    }

    void _emit(bool level, EventSource source, int64_t timestampUs) {
        _events.fetch_add(1, std::memory_order_relaxed);
        _callback(Event{
            .timestampUs = timestampUs,
            .pin = this->config().pin,
            .type = level ? GpioEventType::Rising : GpioEventType::Falling,
            .source = source,
            .level = level,
        });
    }

    [[nodiscard]] static bool _level(uint32_t state) {
        return (state & levelMask) != 0;
    }

    [[nodiscard]] static EventSource _source(uint32_t state) {
        return (state & sourceMask) != 0 ? EventSource::Poll
                                         : EventSource::Interrupt;
    }

    [[nodiscard]] static uint32_t _nextState(uint32_t state, bool level,
                                             EventSource source) {
        return ((state + generationIncrement) & ~(levelMask | sourceMask)) |
               (level ? levelMask : 0U) |
               (source == EventSource::Poll ? sourceMask : 0U);
    }

    void _flushMetrics() {
        const auto events = _events.exchange(0, std::memory_order_relaxed);
        if (events != 0) {
            metrics().addEvents(events);
        }
        const auto isrEvents =
            _isrEvents.exchange(0, std::memory_order_relaxed);
        if (isrEvents != 0) {
            metrics().addIsrEvents(isrEvents);
        }
        const auto pollChanges =
            _pollChanges.exchange(0, std::memory_order_relaxed);
        if (pollChanges != 0) {
            metrics().addPollChanges(pollChanges);
        }
        const auto duplicates =
            _duplicates.exchange(0, std::memory_order_relaxed);
        if (duplicates != 0) {
            metrics().addDuplicates(duplicates);
        }
        const auto debouncedEvents =
            _debouncedEvents.exchange(0, std::memory_order_relaxed);
        if (debouncedEvents != 0) {
            metrics().addDebouncedEvents(debouncedEvents);
        }
    }

    Generic::InlineCallback<Event> _callback;
    ::platform::Gpio _gpio;
    std::atomic<uint32_t> _rawState{0};
    std::atomic<uint32_t> _events{0};
    std::atomic<uint32_t> _isrEvents{0};
    std::atomic<uint32_t> _pollChanges{0};
    std::atomic<uint32_t> _duplicates{0};
    std::atomic<uint32_t> _debouncedEvents{0};
    uint32_t _candidateState = 0;
    uint32_t _candidateSinceMs = 0;
    uint32_t _lastPollMs = 0;
    uint32_t _lastMetricsFlushMs = 0;
    bool _acceptedLevel = false;

    static constexpr LogComponent logComponent =
        Totem::DigitalInput::detail::logComponent;
};

inline constexpr LifecycleContract<DigitalInput, Config>
    _digital_input_lifecycle_contract;

} // namespace Totem::DigitalInput::detail
