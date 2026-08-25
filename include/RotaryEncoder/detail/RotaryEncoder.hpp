// IWYU pragma: private

#pragma once

#include "Base/HasLifecycle.hpp"
#include "DigitalInput/Facade.hpp"
#include "Generic/InlineCallback.hpp"
#include "Macros/Facade.hpp"
#include "RotaryEncoder/Interfaces/Config.hpp"
#include "RotaryEncoder/Interfaces/Types.hpp"
#include "RotaryEncoder/detail/Metrics.hpp"
#include "RotaryEncoder/detail/QuadratureDecoder.hpp"
#include "RotaryEncoder/detail/Types.hpp"
#include "Types/Error.hpp"
#include <atomic>
#include <cstdint>
#include <utility>

namespace Totem::RotaryEncoder::detail {

class RotaryEncoder : public HasLifecycle<RotaryEncoder, Config> {
    using Self = RotaryEncoder;

    friend class HasLifecycle<Self, Config>;
    friend struct LifecycleContract<Self, Config>;

  public:
    template <typename Callback>
    explicit RotaryEncoder(Callback callback)
        : _callback(std::move(callback)),
          _channelA([this](DigitalInput::Event /*event*/) { _handleInput(); }),
          _channelB([this](DigitalInput::Event /*event*/) { _handleInput(); }) {
    }

    DELETE_COPY(RotaryEncoder)
    DELETE_MOVE(RotaryEncoder)

    static constexpr const char *name = "RotaryEncoder";

    ReturnCode work(uint32_t nowMs) {
        FAIL_IF_NOT(this->active(), ERR(InvalidState),
                    "Cannot work inactive rotary encoder");

        auto ret = _channelA.work(nowMs);
        ret.combine(_channelB.work(nowMs));
        if (static_cast<uint32_t>(nowMs - _lastMetricsFlushMs) >=
            metricsFlushIntervalMs) {
            _lastMetricsFlushMs = nowMs;
            _flushMetrics();
        }
        return ret;
    }

    /** Current accepted position, including the configured initial value. */
    [[nodiscard]] int32_t position() const {
        return _position.load(std::memory_order_acquire);
    }

  private:
    ReturnCode _onBegin() {
        prewarmMetrics();
        _ready.store(false, std::memory_order_release);

        FAIL_IF_ERR_FWD(_channelA.begin(this->config().channelA),
                        "Failed to begin rotary encoder channel A");
        const auto channelBRet = _channelB.begin(this->config().channelB);
        if (!channelBRet.ok()) {
            REPORT_IF_ERR(_channelA.end(),
                          "Failed to clean up rotary encoder channel A");
            FAIL_ERR_FWD(channelBRet,
                         "Failed to begin rotary encoder channel B");
        }

        const auto initialA = _channelA.level();
        const auto initialB = _channelB.level();
        if (!initialA || !initialB) {
            auto ret = initialA ? initialB.error() : initialA.error();
            REPORT_IF_ERR(_channelB.end(),
                          "Failed to clean up rotary encoder channel B");
            REPORT_IF_ERR(_channelA.end(),
                          "Failed to clean up rotary encoder channel A");
            FAIL_ERR_FWD(ret, "Failed to read initial rotary encoder state");
        }

        _decoder.reset(_state(*initialA, *initialB));
        _position.store(this->config().position.initialValue,
                        std::memory_order_release);
        _lastMetricsFlushMs = 0;
        _ready.store(true, std::memory_order_release);
        _log_i("Configured rotary encoder on pins " SV_FMT "/" SV_FMT
               "; transitions/detent=%u, reversed=%u",
               MAGIC_SV_ARG(this->config().channelA.pin),
               MAGIC_SV_ARG(this->config().channelB.pin),
               this->config().transitionsPerDetent,
               this->config().reverseDirection ? 1 : 0);
        return OK();
    }

    ReturnCode _onEnd() {
        _ready.store(false, std::memory_order_release);
        auto ret = _channelB.end();
        ret.combine(_channelA.end());
        _flushMetrics();
        return ret;
    }

    void _handleInput() {
        if (!_ready.load(std::memory_order_acquire)) {
            return;
        }

        // Sampling both pins prevents a startup race or a missed edge from
        // leaving the decoder's view of the other channel stale. A resulting
        // diagonal transition is rejected and resynchronizes the decoder.
        const auto channelA = _channelA.level();
        const auto channelB = _channelB.level();
        if (!channelA || !channelB) {
            _invalidTransitions.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const auto result = _decoder.update(_state(*channelA, *channelB),
                                            this->config().transitionsPerDetent,
                                            this->config().reverseDirection);
        if (result.invalidTransition) {
            _invalidTransitions.fetch_add(1, std::memory_order_relaxed);
        }
        if (!result.direction.has_value()) {
            return;
        }

        if (!_advancePosition(*result.direction)) {
            return;
        }

        if (*result.direction == Direction::Clockwise) {
            _clockwiseEvents.fetch_add(1, std::memory_order_relaxed);
        } else {
            _counterclockwiseEvents.fetch_add(1, std::memory_order_relaxed);
        }
        _callback(*result.direction);
    }

    [[nodiscard]] bool _advancePosition(Direction direction) {
        int32_t current = _position.load(std::memory_order_acquire);
        for (;;) {
            const auto next =
                this->config().position.advance(current, direction);
            if (!next.has_value()) {
                return false;
            }
            if (_position.compare_exchange_weak(current, *next,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
                return true;
            }
        }
    }

    [[nodiscard]] static constexpr uint8_t _state(bool channelA,
                                                  bool channelB) {
        return static_cast<uint8_t>((channelA ? 0b10U : 0U) |
                                    (channelB ? 0b01U : 0U));
    }

    void _flushMetrics() {
        const auto clockwise =
            _clockwiseEvents.exchange(0, std::memory_order_relaxed);
        if (clockwise != 0) {
            metrics().addClockwise(clockwise);
        }
        const auto counterclockwise =
            _counterclockwiseEvents.exchange(0, std::memory_order_relaxed);
        if (counterclockwise != 0) {
            metrics().addCounterclockwise(counterclockwise);
        }
        const auto invalid =
            _invalidTransitions.exchange(0, std::memory_order_relaxed);
        if (invalid != 0) {
            metrics().addInvalid(invalid);
        }
    }

    Generic::InlineCallback<Direction> _callback;
    DigitalInput::DigitalInput _channelA;
    DigitalInput::DigitalInput _channelB;
    QuadratureDecoder _decoder{};
    std::atomic<bool> _ready{false};
    std::atomic<int32_t> _position{0};
    std::atomic<uint32_t> _clockwiseEvents{0};
    std::atomic<uint32_t> _counterclockwiseEvents{0};
    std::atomic<uint32_t> _invalidTransitions{0};
    uint32_t _lastMetricsFlushMs = 0;

    static constexpr uint32_t metricsFlushIntervalMs = 100U;
    static constexpr LogComponent logComponent =
        Totem::RotaryEncoder::detail::logComponent;
};

inline constexpr LifecycleContract<RotaryEncoder, Config>
    _rotary_encoder_lifecycle_contract;

} // namespace Totem::RotaryEncoder::detail
