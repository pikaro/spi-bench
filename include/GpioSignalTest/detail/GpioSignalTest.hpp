// IWYU pragma: private

#pragma once

#include "Base/HasLifecycle.hpp"
#include "GpioSignalTest/Interfaces/Config.hpp"
#include "GpioSignalTest/Interfaces/Types.hpp"
#include "GpioSignalTest/detail/PlatformSelect.hpp"
#include "GpioSignalTest/detail/Timing.hpp"
#include "GpioSignalTest/detail/Types.hpp"
#include "Macros/Facade.hpp"
#include "Platform/Gpio.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Types/Error.hpp"
#include <atomic>
#include <cstdint>
#include <expected>
#include <limits>

namespace Totem::GpioSignalTest::detail {

class GpioSignalTest : public HasLifecycle<GpioSignalTest, Config> {
    using Self = GpioSignalTest;

    friend class HasLifecycle<Self, Config>;
    friend struct LifecycleContract<Self, Config>;

  public:
    DELETE_COPY(GpioSignalTest)
    DELETE_MOVE(GpioSignalTest)

    GpioSignalTest() = default;

    static constexpr const char *name = "GpioSignalTest";

    ReturnCode work(uint32_t nowMs) {
        FAIL_IF_NOT(this->active(), ERR(CoreError, InvalidState),
                    "Cannot work inactive GPIO signal test %s",
                    this->config().name);
        if (static_cast<uint32_t>(nowMs - _lastReportMs) <
            this->config().reportIntervalMs) {
            return OK();
        }
        _lastReportMs = nowMs;

        FAIL_IF_UNEXPECTED_FWD(snapshot, status(),
                               "Failed to read GPIO signal status %s",
                               this->config().name);
        if (this->config().role == Role::Producer) {
            _reportProducer(snapshot);
        } else {
            _reportConsumer(snapshot);
        }
        return OK();
    }

    [[nodiscard]] std::expected<Status, ReturnCode> status() const {
        FAIL_IF_NOT(
            this->active(), std::unexpected(ERR(CoreError, InvalidState)),
            "Cannot read inactive GPIO signal test %s", this->config().name);

        if (this->config().role == Role::Producer) {
            const auto transitions = _producer.transitions();
            return Status{
                .role = Role::Producer,
                .pin = this->config().pin,
                .level = _producer.level(),
                .measurementValid = false,
                .risingEdges = (transitions + 1U) / 2U,
                .fallingEdges = transitions / 2U,
                .duplicateEdges = 0,
                .timerErrors = _producer.timerErrors(),
            };
        }

        const auto period = _periodUs.load(std::memory_order_acquire);
        const auto highTime = _highTimeUs.load(std::memory_order_acquire);
        const auto lowTime = _lowTimeUs.load(std::memory_order_acquire);
        const auto minimumPeriod =
            _minimumPeriodUs.load(std::memory_order_acquire);
        const auto lastEdge =
            _lastEdgeTimestampUs.load(std::memory_order_acquire);
        const auto nowUs = static_cast<uint32_t>(::platform::get_time_us());

        return Status{
            .role = Role::Consumer,
            .pin = this->config().pin,
            .level = _lastLevel.load(std::memory_order_acquire),
            .measurementValid = period != 0 && highTime != 0 && lowTime != 0,
            .risingEdges = _risingEdges.load(std::memory_order_acquire),
            .fallingEdges = _fallingEdges.load(std::memory_order_acquire),
            .duplicateEdges = _duplicateEdges.load(std::memory_order_acquire),
            .timerErrors = 0,
            .periodUs = period,
            .minimumPeriodUs =
                minimumPeriod == std::numeric_limits<uint32_t>::max()
                    ? 0
                    : minimumPeriod,
            .maximumPeriodUs = _maximumPeriodUs.load(std::memory_order_acquire),
            .highTimeUs = highTime,
            .lowTimeUs = lowTime,
            .frequencyMilliHz = timing::frequencyMilliHz(period),
            .dutyPartsPerThousand =
                timing::dutyPartsPerThousand(highTime, lowTime),
            .lastEdgeAgeUs = lastEdge == 0
                                 ? std::numeric_limits<uint32_t>::max()
                                 : static_cast<uint32_t>(nowUs - lastEdge),
        };
    }

  private:
    ReturnCode _onBegin() {
        _resetMeasurements();
        _lastReportMs = ::platform::get_time();

        const auto period = timing::periodUs(this->config().frequencyHz);
        const auto highTime =
            timing::highTimeUs(period, this->config().dutyPartsPerThousand);
        const auto lowTime = period - highTime;

        if (this->config().role == Role::Producer) {
            FAIL_IF_ERR_FWD(_producer.init(this->config().pin,
                                           this->config().name, highTime,
                                           lowTime),
                            "Failed to initialize GPIO signal producer %s",
                            this->config().name);
            _log_i("GPIO signal producer ready name=%s pin=" SV_FMT
                   " frequencyHz=%lu dutyPpt=%u highUs=%lu lowUs=%lu",
                   this->config().name, MAGIC_SV_ARG(this->config().pin),
                   static_cast<unsigned long>(this->config().frequencyHz),
                   static_cast<unsigned>(this->config().dutyPartsPerThousand),
                   static_cast<unsigned long>(highTime),
                   static_cast<unsigned long>(lowTime));
        } else {
            FAIL_IF_ERR_FWD(_consumerGpio.initInput(this->config().pin,
                                                    this->config().consumerPull,
                                                    GpioInterrupt::AnyEdge),
                            "Failed to initialize GPIO signal consumer %s",
                            this->config().name);
            FAIL_IF_UNEXPECTED_FWD(
                initialLevel, _consumerGpio.level(),
                "Failed to read initial GPIO signal level %s",
                this->config().name);
            _lastLevel.store(initialLevel, std::memory_order_relaxed);
            FAIL_IF_ERR_FWD(_consumerGpio.registerIsr(this, _onEdge),
                            "Failed to register GPIO signal edge consumer %s",
                            this->config().name);
            _log_i("GPIO signal consumer ready name=%s pin=" SV_FMT
                   " expectedHz=%lu expectedDutyPpt=%u initialLevel=%u",
                   this->config().name, MAGIC_SV_ARG(this->config().pin),
                   static_cast<unsigned long>(this->config().frequencyHz),
                   static_cast<unsigned>(this->config().dutyPartsPerThousand),
                   initialLevel ? 1U : 0U);
        }

        if (this->config().dumpPinConfiguration) {
            REPORT_IF_ERR(platform::dumpPinConfiguration(this->config().pin),
                          "Failed to dump GPIO signal pin %s",
                          this->config().name);
        }
        return OK();
    }

    ReturnCode _onEnd() {
        if (this->config().role == Role::Producer) {
            return _producer.deinit();
        }
        auto ret = _consumerGpio.deinit();
        _resetMeasurements();
        return ret;
    }

    void _reportProducer(const Status &snapshot) {
        const auto transitions = snapshot.risingEdges + snapshot.fallingEdges;
        if (snapshot.timerErrors != 0) {
            _log_w(
                "GPIO signal producer status name=%s pin=" SV_FMT
                " expectedMilliHz=%lu dutyPpt=%u level=%u edges=%lu "
                "timerErrors=%lu result=bad",
                this->config().name, MAGIC_SV_ARG(snapshot.pin),
                static_cast<unsigned long>(this->config().frequencyHz * 1'000U),
                static_cast<unsigned>(this->config().dutyPartsPerThousand),
                snapshot.level ? 1U : 0U,
                static_cast<unsigned long>(transitions),
                static_cast<unsigned long>(snapshot.timerErrors));
        } else {
            _log_i(
                "GPIO signal producer status name=%s pin=" SV_FMT
                " expectedMilliHz=%lu dutyPpt=%u level=%u edges=%lu "
                "timerErrors=%lu result=ok",
                this->config().name, MAGIC_SV_ARG(snapshot.pin),
                static_cast<unsigned long>(this->config().frequencyHz * 1'000U),
                static_cast<unsigned>(this->config().dutyPartsPerThousand),
                snapshot.level ? 1U : 0U,
                static_cast<unsigned long>(transitions),
                static_cast<unsigned long>(snapshot.timerErrors));
        }
    }

    void _reportConsumer(const Status &snapshot) {
        const auto expectedMilliHz = this->config().frequencyHz * 1'000U;
        const auto frequencyError = timing::errorPartsPerThousand(
            snapshot.frequencyMilliHz, expectedMilliHz);
        const auto dutyError = timing::absoluteDifference(
            snapshot.dutyPartsPerThousand, this->config().dutyPartsPerThousand);
        const auto expectedPeriod =
            timing::periodUs(this->config().frequencyHz);
        const auto staleThreshold =
            expectedPeriod * this->config().stalePeriodCount;
        const auto newDuplicates =
            snapshot.duplicateEdges - _lastReportedDuplicateEdges;
        const bool stale =
            snapshot.lastEdgeAgeUs == std::numeric_limits<uint32_t>::max() ||
            snapshot.lastEdgeAgeUs > staleThreshold;
        const bool bad =
            !snapshot.measurementValid || stale ||
            frequencyError >
                this->config().frequencyTolerancePartsPerThousand ||
            dutyError > this->config().dutyTolerancePartsPerThousand ||
            newDuplicates != 0;
        const auto spread =
            snapshot.maximumPeriodUs >= snapshot.minimumPeriodUs
                ? snapshot.maximumPeriodUs - snapshot.minimumPeriodUs
                : 0U;
        const auto spreadPartsPerThousand =
            expectedPeriod == 0
                ? 0U
                : static_cast<uint32_t>((static_cast<uint64_t>(spread) *
                                         timing::partsPerThousand) /
                                        expectedPeriod);
        const bool unstable =
            spreadPartsPerThousand >
            this->config().periodSpreadTolerancePartsPerThousand;

        const bool resultBad = bad || unstable;

        if (resultBad) {
            _log_w("GPIO signal consumer status name=%s pin=" SV_FMT
                   " expectedMilliHz=%lu observedMilliHz=%lu freqErrorPpt=%lu "
                   "dutyPpt=%u periodUs=%lu minUs=%lu maxUs=%lu spreadUs=%lu "
                   "spreadPpt=%lu "
                   "highUs=%lu lowUs=%lu rising=%lu falling=%lu duplicates=%lu "
                   "ageUs=%lu level=%u result=bad",
                   this->config().name, MAGIC_SV_ARG(snapshot.pin),
                   static_cast<unsigned long>(expectedMilliHz),
                   static_cast<unsigned long>(snapshot.frequencyMilliHz),
                   static_cast<unsigned long>(frequencyError),
                   static_cast<unsigned>(snapshot.dutyPartsPerThousand),
                   static_cast<unsigned long>(snapshot.periodUs),
                   static_cast<unsigned long>(snapshot.minimumPeriodUs),
                   static_cast<unsigned long>(snapshot.maximumPeriodUs),
                   static_cast<unsigned long>(spread),
                   static_cast<unsigned long>(spreadPartsPerThousand),
                   static_cast<unsigned long>(snapshot.highTimeUs),
                   static_cast<unsigned long>(snapshot.lowTimeUs),
                   static_cast<unsigned long>(snapshot.risingEdges),
                   static_cast<unsigned long>(snapshot.fallingEdges),
                   static_cast<unsigned long>(snapshot.duplicateEdges),
                   static_cast<unsigned long>(snapshot.lastEdgeAgeUs),
                   snapshot.level ? 1U : 0U);
        } else {
            _log_i("GPIO signal consumer status name=%s pin=" SV_FMT
                   " expectedMilliHz=%lu observedMilliHz=%lu freqErrorPpt=%lu "
                   "dutyPpt=%u periodUs=%lu minUs=%lu maxUs=%lu spreadUs=%lu "
                   "spreadPpt=%lu "
                   "highUs=%lu lowUs=%lu rising=%lu falling=%lu duplicates=%lu "
                   "ageUs=%lu level=%u result=ok",
                   this->config().name, MAGIC_SV_ARG(snapshot.pin),
                   static_cast<unsigned long>(expectedMilliHz),
                   static_cast<unsigned long>(snapshot.frequencyMilliHz),
                   static_cast<unsigned long>(frequencyError),
                   static_cast<unsigned>(snapshot.dutyPartsPerThousand),
                   static_cast<unsigned long>(snapshot.periodUs),
                   static_cast<unsigned long>(snapshot.minimumPeriodUs),
                   static_cast<unsigned long>(snapshot.maximumPeriodUs),
                   static_cast<unsigned long>(spread),
                   static_cast<unsigned long>(spreadPartsPerThousand),
                   static_cast<unsigned long>(snapshot.highTimeUs),
                   static_cast<unsigned long>(snapshot.lowTimeUs),
                   static_cast<unsigned long>(snapshot.risingEdges),
                   static_cast<unsigned long>(snapshot.fallingEdges),
                   static_cast<unsigned long>(snapshot.duplicateEdges),
                   static_cast<unsigned long>(snapshot.lastEdgeAgeUs),
                   snapshot.level ? 1U : 0U);
        }
        _lastReportedDuplicateEdges = snapshot.duplicateEdges;
    }

    static void _onEdge(void *owner, GpioEvent event) {
        auto *self = static_cast<Self *>(owner);
        if (self == nullptr) {
            return;
        }
        self->_handleEdge(event);
    }

    void _handleEdge(GpioEvent event) {
        const auto timestampUs = static_cast<uint32_t>(event.timestampUs);
        const auto previousLevel =
            _lastLevel.exchange(event.level, std::memory_order_acq_rel);
        if (previousLevel == event.level) {
            _duplicateEdges.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        _lastEdgeTimestampUs.store(timestampUs, std::memory_order_release);

        if (event.level) {
            _risingEdges.fetch_add(1, std::memory_order_relaxed);
            const auto previousRise = _lastRisingTimestampUs.exchange(
                timestampUs, std::memory_order_acq_rel);
            if (previousRise != 0) {
                const auto period =
                    static_cast<uint32_t>(timestampUs - previousRise);
                _periodUs.store(period, std::memory_order_release);
                _updateMinimum(_minimumPeriodUs, period);
                _updateMaximum(_maximumPeriodUs, period);
            }
            const auto previousFall =
                _lastFallingTimestampUs.load(std::memory_order_acquire);
            if (previousFall != 0) {
                _lowTimeUs.store(
                    static_cast<uint32_t>(timestampUs - previousFall),
                    std::memory_order_release);
            }
        } else {
            _fallingEdges.fetch_add(1, std::memory_order_relaxed);
            const auto previousRise =
                _lastRisingTimestampUs.load(std::memory_order_acquire);
            _lastFallingTimestampUs.store(timestampUs,
                                          std::memory_order_release);
            if (previousRise != 0) {
                _highTimeUs.store(
                    static_cast<uint32_t>(timestampUs - previousRise),
                    std::memory_order_release);
            }
        }
    }

    static void _updateMinimum(std::atomic<uint32_t> &target, uint32_t value) {
        auto current = target.load(std::memory_order_relaxed);
        while (value < current && !target.compare_exchange_weak(
                                      current, value, std::memory_order_relaxed,
                                      std::memory_order_relaxed)) {
        }
    }

    static void _updateMaximum(std::atomic<uint32_t> &target, uint32_t value) {
        auto current = target.load(std::memory_order_relaxed);
        while (value > current && !target.compare_exchange_weak(
                                      current, value, std::memory_order_relaxed,
                                      std::memory_order_relaxed)) {
        }
    }

    void _resetMeasurements() {
        _lastLevel.store(false, std::memory_order_relaxed);
        _risingEdges.store(0, std::memory_order_relaxed);
        _fallingEdges.store(0, std::memory_order_relaxed);
        _duplicateEdges.store(0, std::memory_order_relaxed);
        _lastEdgeTimestampUs.store(0, std::memory_order_relaxed);
        _lastRisingTimestampUs.store(0, std::memory_order_relaxed);
        _lastFallingTimestampUs.store(0, std::memory_order_relaxed);
        _periodUs.store(0, std::memory_order_relaxed);
        _minimumPeriodUs.store(std::numeric_limits<uint32_t>::max(),
                               std::memory_order_relaxed);
        _maximumPeriodUs.store(0, std::memory_order_relaxed);
        _highTimeUs.store(0, std::memory_order_relaxed);
        _lowTimeUs.store(0, std::memory_order_relaxed);
        _lastReportedDuplicateEdges = 0;
    }

    platform::Producer _producer;
    ::platform::Gpio _consumerGpio;
    std::atomic<bool> _lastLevel{false};
    std::atomic<uint32_t> _risingEdges{0};
    std::atomic<uint32_t> _fallingEdges{0};
    std::atomic<uint32_t> _duplicateEdges{0};
    std::atomic<uint32_t> _lastEdgeTimestampUs{0};
    std::atomic<uint32_t> _lastRisingTimestampUs{0};
    std::atomic<uint32_t> _lastFallingTimestampUs{0};
    std::atomic<uint32_t> _periodUs{0};
    std::atomic<uint32_t> _minimumPeriodUs{
        std::numeric_limits<uint32_t>::max()};
    std::atomic<uint32_t> _maximumPeriodUs{0};
    std::atomic<uint32_t> _highTimeUs{0};
    std::atomic<uint32_t> _lowTimeUs{0};
    uint32_t _lastReportMs = 0;
    uint32_t _lastReportedDuplicateEdges = 0;

    static constexpr LogComponent logComponent = detail::logComponent;
};

inline constexpr LifecycleContract<GpioSignalTest, Config>
    _gpio_signal_test_lifecycle_contract;

} // namespace Totem::GpioSignalTest::detail
