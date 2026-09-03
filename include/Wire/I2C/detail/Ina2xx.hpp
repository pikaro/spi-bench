// IWYU pragma: private

#pragma once

#include "Base/HasLifecycle.hpp"
#include "DigitalInput/Facade.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include "Wire/I2C/Interfaces/Ina2xxConfig.hpp"
#include "Wire/I2C/detail/Device.hpp"
#include "Wire/I2C/detail/Ina2xxMetrics.hpp"
#include "Wire/I2C/detail/Master.hpp"
#include "Wire/I2C/detail/Types.hpp"
#include "Wire/I2C/detail/ina2xx/Conversion.hpp"
#include "Wire/I2C/detail/ina2xx/Registers.hpp"
#include <array>
#include <atomic>
#include <bit>
#include <cinttypes>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <string_view>
#include <utility>

namespace Totem::Wire::I2C::detail {

class Ina2xx : public HasLifecycle<Ina2xx, Ina2xxConfig> {
    using Self = Ina2xx;

    friend class HasLifecycle<Self, Ina2xxConfig>;
    friend struct LifecycleContract<Self, Ina2xxConfig>;

  public:
    DELETE_COPY(Ina2xx)
    DELETE_MOVE(Ina2xx)

    static constexpr const char *name = "I2C::Ina2xx";
    static constexpr LogComponent logComponent =
        Totem::Wire::I2C::detail::logComponent;

    explicit Ina2xx(Master &master, Ina2xxModel model)
        : _master(master), _model(model),
          _alertInput([this](Totem::DigitalInput::Event event) {
              _handleAlertPin(event);
          }) {}

    [[nodiscard]] Ina2xxModel model() const { return _model; }
    [[nodiscard]] uint32_t capabilities() const {
        return ina2xx_capabilities(_model);
    }
    [[nodiscard]] bool supports(Ina2xxCapability capability) const {
        return ina2xx_supports(_model, capability);
    }

    ReturnCode setLimitCallback(Ina2xxLimitCallback callback) {
        FAIL_IF_ACTIVE_ERR("Cannot change active INA2xx limit callback");
        _limitCallback.emplace(std::move(callback));
        return OK();
    }

    template <typename Callback>
        requires std::constructible_from<Ina2xxLimitCallback, Callback>
    ReturnCode setLimitCallback(Callback callback) {
        return setLimitCallback(Ina2xxLimitCallback{std::move(callback)});
    }

    ReturnCode setAlertCallback(Ina2xxAlertCallback callback) {
        FAIL_IF(!supports(Ina2xxCapability::HardwareAlert),
                ERR(CoreError, NotSupported),
                "INA2xx model does not support a hardware ALERT callback");
        FAIL_IF_ACTIVE_ERR("Cannot change active INA2xx ALERT callback");
        _alertCallback.emplace(std::move(callback));
        return OK();
    }

    template <typename Callback>
        requires std::constructible_from<Ina2xxAlertCallback, Callback>
    ReturnCode setAlertCallback(Callback callback) {
        return setAlertCallback(Ina2xxAlertCallback{std::move(callback)});
    }

    /**
     * Registers a bounded callback for each successfully converted sample.
     * The callback runs synchronously from work() and must not block or access
     * a filesystem.
     */
    ReturnCode setSampleCallback(Ina2xxSampleCallback callback) {
        FAIL_IF_ACTIVE_ERR("Cannot change active INA2xx sample callback");
        _sampleCallback.emplace(std::move(callback));
        return OK();
    }

    template <typename Callback>
        requires std::constructible_from<Ina2xxSampleCallback, Callback>
    ReturnCode setSampleCallback(Callback callback) {
        return setSampleCallback(Ina2xxSampleCallback{std::move(callback)});
    }

    ReturnCode clearLimitCallback() {
        FAIL_IF_ACTIVE_ERR("Cannot change active INA2xx limit callback");
        _limitCallback.reset();
        return OK();
    }

    ReturnCode clearAlertCallback() {
        FAIL_IF_ACTIVE_ERR("Cannot change active INA2xx ALERT callback");
        _alertCallback.reset();
        return OK();
    }

    ReturnCode clearSampleCallback() {
        FAIL_IF_ACTIVE_ERR("Cannot change active INA2xx sample callback");
        _sampleCallback.reset();
        return OK();
    }

    ReturnCode work(uint32_t nowMs) {
        FAIL_IF_INACTIVE_ERR("Cannot work inactive INA2xx sensor");

        auto ret = OK();
        if (_alertInput.active()) {
            ret.combine(_alertInput.work(nowMs));
        }

        const bool alertPending =
            _alertPending.exchange(false, std::memory_order_acq_rel);
        const bool sampleDue =
            !_attemptedSample ||
            static_cast<uint32_t>(nowMs - _lastSampleAttemptMs) >=
                config().sampleIntervalMs;
        if (alertPending || sampleDue) {
            ret.combine(_sample(nowMs));
        }
        _recordAge(nowMs);
        return ret;
    }

    [[nodiscard]] std::expected<Ina2xxSample, ReturnCode>
    sampleNow(uint32_t nowMs) {
        FAIL_IF_INACTIVE_UNEXPECTED("Cannot sample inactive INA2xx sensor");
        auto ret = _sample(nowMs);
        _recordAge(nowMs);
        if (!ret.ok()) {
            return std::unexpected(ret);
        }
        return _latestSample;
    }

    [[nodiscard]] std::expected<Ina2xxSample, ReturnCode> latestSample() const {
        FAIL_IF(!_hasSample, std::unexpected(ERR(CoreError, NotFinished)),
                "INA2xx sensor does not have a valid sample yet");
        return _latestSample;
    }

    [[nodiscard]] Ina2xxLimitState
    limitState(Ina2xxLimitMeasurement measurement) const {
        switch (measurement) {
        case Ina2xxLimitMeasurement::BusVoltage:
            return _busVoltageLimitState;
        case Ina2xxLimitMeasurement::Current:
            return _currentLimitState;
        }
        return Ina2xxLimitState::Normal;
    }

  private:
    struct RawSample {
        uint16_t shunt = 0;
        uint16_t bus = 0;
        uint16_t current = 0;
        uint16_t maskEnable = 0;
    };

    ReturnCode _onBegin() {
        FAIL_IF(!_master.active(), ERR(CoreError, InvalidState),
                "Cannot begin INA2xx before I2C master is active");
        FAIL_IF_ERR_FWD(_validateModelConfig(), "Invalid INA2xx model config");
        FAIL_IF_ERR_FWD(_prepareMetrics(), "Failed to prepare INA2xx metrics");

        FAIL_IF_ERR_FWD(_device.begin(_master, config().device),
                        "Failed to register INA2xx I2C device");
        auto ret = _configureDevice();
        if (!ret.ok()) {
            (void)_device.end();
            return ret;
        }

        if (config().hardwareAlert.has_value()) {
            ret = _beginAlertInput(*config().hardwareAlert);
            if (!ret.ok()) {
                (void)_writeRegister(ina2xx::Register::MaskEnable, 0);
                (void)_device.end();
                return ret;
            }
        }

        _attemptedSample = false;
        _hasSample = false;
        _hasBusVoltageLimitState = false;
        _hasCurrentLimitState = false;
        _busVoltageLimitState = Ina2xxLimitState::Normal;
        _currentLimitState = Ina2xxLimitState::Normal;
        _hardwareAlertCondition = false;
        _alertPending.store(false, std::memory_order_relaxed);
        _metrics->initialize(capabilities());

        if (_alertInput.active()) {
            auto level = _alertInput.level();
            if (!level) {
                auto levelRet = level.error();
                levelRet.combine(_alertInput.end());
                levelRet.combine(
                    _writeRegister(ina2xx::Register::MaskEnable, 0));
                levelRet.combine(_device.end());
                return levelRet;
            }
            if (*level == config().hardwareAlert->activeHigh) {
                _alertPending.store(true, std::memory_order_release);
            }
        }

        _log_i("Initialized " SV_FMT " at 0x%02X; interval=%" PRIu32
               " ms, shunt=%" PRIu32 " uOhm",
               MAGIC_SV_ARG(_model), config().device.address,
               config().sampleIntervalMs, config().shuntMicroOhms);
        return OK();
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        if (_model == Ina2xxModel::Ina226 && _device.active()) {
            ret.combine(_writeRegister(ina2xx::Register::MaskEnable, 0));
        }
        if (_alertInput.active()) {
            ret.combine(_alertInput.end());
        }
        ret.combine(_device.end());

        _attemptedSample = false;
        _hasSample = false;
        _hasBusVoltageLimitState = false;
        _hasCurrentLimitState = false;
        _busVoltageLimitState = Ina2xxLimitState::Normal;
        _currentLimitState = Ina2xxLimitState::Normal;
        _latestSample = {};
        _expectedShuntMicrovolts = 0;
        _hardwareAlertCondition = false;
        _alertPending.store(false, std::memory_order_relaxed);
        if (_metrics.has_value()) {
            _metrics->initialize(capabilities());
        }
        return ret;
    }

    ReturnCode _validateModelConfig() {
        FAIL_IF(_model == Ina2xxModel::Ina228, ERR(CoreError, NotSupported),
                "INA228 is the production target but its 20-bit/24-bit/40-bit "
                "register backend is not implemented yet");
        const uint32_t maximumBusMillivolts =
            _model == Ina2xxModel::Ina219 ? 26000U : 36000U;
        FAIL_IF(
            config().busVoltage.absoluteMaxMillivolts > maximumBusMillivolts,
            ERR(CoreError, InvalidArgument),
            "INA2xx bus-voltage limit exceeds model maximum of %" PRIu32 " mV",
            maximumBusMillivolts);

        auto expectedShunt = ina2xx::expected_shunt_microvolts(
            config().expectedMaxCurrentMicroamps, config().shuntMicroOhms);
        if (!expectedShunt) {
            return ReturnCode::from(expectedShunt.error());
        }
        _expectedShuntMicrovolts = *expectedShunt;

        auto minimumCurrentShunt = ina2xx::current_to_shunt_microvolts(
            config().current.absoluteMinMicroamps, config().shuntMicroOhms);
        if (!minimumCurrentShunt) {
            return ReturnCode::from(minimumCurrentShunt.error());
        }
        auto maximumCurrentShunt = ina2xx::current_to_shunt_microvolts(
            config().current.absoluteMaxMicroamps, config().shuntMicroOhms);
        if (!maximumCurrentShunt) {
            return ReturnCode::from(maximumCurrentShunt.error());
        }

        switch (_model) {
        case Ina2xxModel::Ina219:
            FAIL_IF(*minimumCurrentShunt < -320000 ||
                        *maximumCurrentShunt > 319990 ||
                        _expectedShuntMicrovolts > 319990,
                    ERR(CoreError, InvalidArgument),
                    "INA219 expected current or current limits exceed its "
                    "shunt range");
            break;
        case Ina2xxModel::Ina226:
            FAIL_IF(*minimumCurrentShunt < -81920 ||
                        *maximumCurrentShunt > 81917 ||
                        _expectedShuntMicrovolts > 81917,
                    ERR(CoreError, InvalidArgument),
                    "INA226 expected current or current limits exceed its "
                    "shunt range");
            break;
        case Ina2xxModel::Ina228:
            return ERR(CoreError, NotSupported);
        }

        FAIL_IF(config().hardwareAlert.has_value() &&
                    !supports(Ina2xxCapability::HardwareAlert),
                ERR(CoreError, NotSupported),
                "Selected INA2xx model does not support hardware ALERT");
        FAIL_IF(config().hardwareAlert.has_value() !=
                    _alertCallback.has_value(),
                ERR(CoreError, InvalidArgument),
                "INA2xx hardware ALERT config and callback must be provided "
                "together");

        auto calibration =
            ina2xx::make_calibration(_model, config().shuntMicroOhms,
                                     config().expectedMaxCurrentMicroamps);
        if (!calibration) {
            return ReturnCode::from(calibration.error());
        }
        _calibration = *calibration;

        const auto minimumMeasuredCurrent =
            -INT64_C(32768) * _calibration.currentLsbMicroamps;
        const auto maximumMeasuredCurrent =
            INT64_C(32767) * _calibration.currentLsbMicroamps;
        FAIL_IF(
            config().current.absoluteMinMicroamps < minimumMeasuredCurrent ||
                config().current.absoluteMaxMicroamps > maximumMeasuredCurrent,
            ERR(CoreError, InvalidArgument),
            "INA2xx current limits exceed calibrated range %" PRId64
            "..%" PRId64 " uA",
            minimumMeasuredCurrent, maximumMeasuredCurrent);
        return OK();
    }

    ReturnCode _prepareMetrics() {
        const std::string_view requestedName{config().metricsGroupName};
        if (_metrics.has_value()) {
            FAIL_IF(std::string_view{_metricsName.data()} != requestedName,
                    ERR(CoreError, InvalidArgument),
                    "INA2xx metrics group name cannot change after first "
                    "begin");
            return OK();
        }

        for (size_t i = 0; i < requestedName.size(); ++i) {
            _metricsName[i] = requestedName[i];
        }
        _metricsName[requestedName.size()] = '\0';
        _metricsGroupDesc.name = _metricsName.data();
        _metrics.emplace(Ina2xxMetrics::create(_metricsGroupDesc));
        return OK();
    }

    ReturnCode _configureDevice() {
        switch (_model) {
        case Ina2xxModel::Ina219:
            FAIL_IF_ERR_FWD(_writeRegister(ina2xx::Register::Configuration,
                                           _ina219Configuration()),
                            "Failed to configure INA219");
            return _writeRegister(ina2xx::Register::Calibration,
                                  _calibration.registerValue);
        case Ina2xxModel::Ina226:
            FAIL_IF_ERR_FWD(_verifyIna226Identity(),
                            "INA226 identity verification failed");
            FAIL_IF_ERR_FWD(_writeRegister(ina2xx::Register::Configuration,
                                           ina2xx::ina226DefaultConfiguration),
                            "Failed to configure INA226");
            FAIL_IF_ERR_FWD(_writeRegister(ina2xx::Register::Calibration,
                                           _calibration.registerValue),
                            "Failed to calibrate INA226");
            return _configureIna226Alert();
        case Ina2xxModel::Ina228:
            return ERR(CoreError, NotSupported);
        }
        return ERR(CoreError, NotSupported);
    }

    ReturnCode _verifyIna226Identity() {
        auto manufacturer = _readRegister(ina2xx::Register::ManufacturerId);
        if (!manufacturer) {
            return manufacturer.error();
        }
        FAIL_IF(*manufacturer != ina2xx::ina226ManufacturerId,
                ERR(CoreError, InvalidResponse),
                "Unexpected INA226 manufacturer ID 0x%04X", *manufacturer);

        auto die = _readRegister(ina2xx::Register::DieId);
        if (!die) {
            return die.error();
        }
        FAIL_IF((*die & ina2xx::ina226DieIdMask) != ina2xx::ina226DieId,
                ERR(CoreError, InvalidResponse),
                "Unexpected INA226 die ID 0x%04X", *die);
        return OK();
    }

    ReturnCode _configureIna226Alert() {
        if (!config().hardwareAlert.has_value()) {
            return _writeRegister(ina2xx::Register::MaskEnable, 0);
        }

        const auto function = config().hardwareAlert->function;
        uint16_t rawLimit = 0;
        uint16_t mask = 0;
        switch (function) {
        case Ina2xxAlertFunction::BusVoltageOver:
            rawLimit = ina2xx::ina226_bus_alert_raw(
                config().busVoltage.practicalMaxMillivolts);
            mask = ina2xx::ina226BusOver;
            break;
        case Ina2xxAlertFunction::BusVoltageUnder:
            rawLimit = ina2xx::ina226_bus_alert_raw(
                config().busVoltage.practicalMinMillivolts);
            mask = ina2xx::ina226BusUnder;
            break;
        case Ina2xxAlertFunction::CurrentOver:
        case Ina2xxAlertFunction::CurrentUnder: {
            const int32_t thresholdMicroamps =
                function == Ina2xxAlertFunction::CurrentOver
                    ? config().current.practicalMaxMicroamps
                    : config().current.practicalMinMicroamps;
            auto thresholdMicrovolts = ina2xx::current_to_shunt_microvolts(
                thresholdMicroamps, config().shuntMicroOhms);
            if (!thresholdMicrovolts) {
                return ReturnCode::from(thresholdMicrovolts.error());
            }
            rawLimit = std::bit_cast<uint16_t>(
                ina2xx::ina226_shunt_alert_raw(*thresholdMicrovolts));
            mask = function == Ina2xxAlertFunction::CurrentOver
                       ? ina2xx::ina226ShuntOver
                       : ina2xx::ina226ShuntUnder;
            break;
        }
        }
        FAIL_IF_ERR_FWD(_writeRegister(ina2xx::Register::AlertLimit, rawLimit),
                        "Failed to configure INA226 alert limit");

        if (config().hardwareAlert->activeHigh) {
            mask |= ina2xx::ina226AlertPolarity;
        }
        if (config().hardwareAlert->latched) {
            mask |= ina2xx::ina226AlertLatchEnable;
        }
        return _writeRegister(ina2xx::Register::MaskEnable, mask);
    }

    ReturnCode _beginAlertInput(const Ina2xxHardwareAlertConfig &alertConfig) {
        const Totem::DigitalInput::Config inputConfig{
            .name = "inaAlert",
            .pin = alertConfig.pin,
            .pull = alertConfig.activeHigh ? GpioPull::Down : GpioPull::Up,
            .debounceMs = 1,
            .pollIntervalMs = 100,
        };
        return _alertInput.begin(inputConfig);
    }

    void _handleAlertPin(const Totem::DigitalInput::Event &event) {
        if (config().hardwareAlert.has_value() &&
            event.level == config().hardwareAlert->activeHigh) {
            _alertPending.store(true, std::memory_order_release);
        }
    }

    ReturnCode _sample(uint32_t nowMs) {
        _attemptedSample = true;
        _lastSampleAttemptMs = nowMs;

        auto raw = _readRawSample();
        if (!raw) {
            _metrics->addFailure();
            return raw.error();
        }

        const bool overflow =
            _model == Ina2xxModel::Ina219
                ? ((raw->bus & ina2xx::ina219BusOverflow) != 0)
                : ((raw->maskEnable & ina2xx::ina226MathOverflowFlag) != 0);
        if (overflow) {
            _metrics->addFailure();
            _metrics->addOverflow();
            _emitHardwareAlert(raw->maskEnable);
            return ERR(CoreError, Overflow);
        }

        const auto signedShunt = std::bit_cast<int16_t>(raw->shunt);
        const auto signedCurrent = std::bit_cast<int16_t>(raw->current);
        const int32_t shuntMicrovolts =
            _model == Ina2xxModel::Ina219
                ? ina2xx::ina219_shunt_microvolts(signedShunt)
                : ina2xx::ina226_shunt_microvolts(signedShunt);
        const uint32_t busMillivolts =
            _model == Ina2xxModel::Ina219
                ? ina2xx::ina219_bus_millivolts(raw->bus)
                : ina2xx::ina226_bus_millivolts(raw->bus);

        const auto currentWide = static_cast<int64_t>(signedCurrent) *
                                 _calibration.currentLsbMicroamps;
        if (currentWide < std::numeric_limits<int32_t>::min() ||
            currentWide > std::numeric_limits<int32_t>::max()) {
            _metrics->addFailure();
            _metrics->addOverflow();
            return ERR(CoreError, Overflow);
        }
        const auto powerWide =
            (static_cast<int64_t>(busMillivolts) * currentWide) / 1000000;
        if (powerWide < std::numeric_limits<int32_t>::min() ||
            powerWide > std::numeric_limits<int32_t>::max()) {
            _metrics->addFailure();
            _metrics->addOverflow();
            return ERR(CoreError, Overflow);
        }

        _latestSample = {
            .shuntMicrovolts = shuntMicrovolts,
            .busMillivolts = busMillivolts,
            .currentMicroamps = static_cast<int32_t>(currentWide),
            .powerMilliwatts = static_cast<int32_t>(powerWide),
            .capturedAtMs = nowMs,
            .validCapabilities =
                ina2xx_capability_mask(Ina2xxCapability::ShuntVoltage) |
                ina2xx_capability_mask(Ina2xxCapability::BusVoltage) |
                ina2xx_capability_mask(Ina2xxCapability::Current) |
                ina2xx_capability_mask(Ina2xxCapability::Power),
        };
        _hasSample = true;

        const auto nextBusVoltageState = ina2xx::classify_bus_voltage(
            _latestSample.busMillivolts, config().busVoltage);
        const auto nextCurrentState = ina2xx::classify_current(
            _latestSample.currentMicroamps, config().current);
        _updateLimitState(Ina2xxLimitMeasurement::BusVoltage,
                          nextBusVoltageState);
        _updateLimitState(Ina2xxLimitMeasurement::Current, nextCurrentState);
        _metrics->recordSample(_latestSample, nextBusVoltageState,
                               nextCurrentState);
        if (_sampleCallback.has_value()) {
            (*_sampleCallback)(_latestSample);
        }
        _emitHardwareAlert(raw->maskEnable);

        return OK();
    }

    std::expected<RawSample, ReturnCode> _readRawSample() {
        auto shunt = _readRegister(ina2xx::Register::ShuntVoltage);
        if (!shunt) {
            return std::unexpected(shunt.error());
        }
        auto bus = _readRegister(ina2xx::Register::BusVoltage);
        if (!bus) {
            return std::unexpected(bus.error());
        }
        auto current = _readRegister(ina2xx::Register::Current);
        if (!current) {
            return std::unexpected(current.error());
        }

        uint16_t maskEnable = 0;
        if (_model == Ina2xxModel::Ina226) {
            auto mask = _readRegister(ina2xx::Register::MaskEnable);
            if (!mask) {
                return std::unexpected(mask.error());
            }
            maskEnable = *mask;
        }
        return RawSample{
            .shunt = *shunt,
            .bus = *bus,
            .current = *current,
            .maskEnable = maskEnable,
        };
    }

    void _updateLimitState(Ina2xxLimitMeasurement measurement,
                           Ina2xxLimitState nextState) {
        auto &trackedState = measurement == Ina2xxLimitMeasurement::BusVoltage
                                 ? _busVoltageLimitState
                                 : _currentLimitState;
        auto &hasTrackedState =
            measurement == Ina2xxLimitMeasurement::BusVoltage
                ? _hasBusVoltageLimitState
                : _hasCurrentLimitState;
        const auto previous = trackedState;
        if (!hasTrackedState) {
            hasTrackedState = true;
            trackedState = nextState;
            if (nextState == Ina2xxLimitState::Normal) {
                return;
            }
        } else if (previous == nextState) {
            return;
        } else {
            trackedState = nextState;
        }

        const auto nextSeverity = ina2xx::severity(nextState);
        const int64_t value =
            measurement == Ina2xxLimitMeasurement::BusVoltage
                ? static_cast<int64_t>(_latestSample.busMillivolts)
                : _latestSample.currentMicroamps;
        const char *unit =
            measurement == Ina2xxLimitMeasurement::BusVoltage ? "mV" : "uA";
        switch (nextSeverity) {
        case Ina2xxLimitSeverity::Normal:
            _log_i("INA2xx " SV_FMT " returned to normal at %" PRId64 " %s",
                   MAGIC_SV_ARG(measurement), value, unit);
            break;
        case Ina2xxLimitSeverity::Warning:
            _log_w("INA2xx practical " SV_FMT " limit crossed: state=" SV_FMT
                   ", value=%" PRId64 " %s",
                   MAGIC_SV_ARG(measurement), MAGIC_SV_ARG(nextState), value,
                   unit);
            break;
        case Ina2xxLimitSeverity::Error:
            _log_e("INA2xx absolute " SV_FMT " limit crossed: state=" SV_FMT
                   ", value=%" PRId64 " %s",
                   MAGIC_SV_ARG(measurement), MAGIC_SV_ARG(nextState), value,
                   unit);
            break;
        }

        if (_limitCallback.has_value()) {
            const Ina2xxLimitEvent event{
                .measurement = measurement,
                .previous = previous,
                .current = nextState,
                .severity = nextSeverity,
                .sample = _latestSample,
            };
            (*_limitCallback)(event);
        }
    }

    void _emitHardwareAlert(uint16_t maskEnable) {
        if (!config().hardwareAlert.has_value()) {
            return;
        }

        const auto &alertConfig = *config().hardwareAlert;
        bool condition = false;
        if (_hasSample) {
            switch (alertConfig.function) {
            case Ina2xxAlertFunction::BusVoltageOver:
                condition = _latestSample.busMillivolts >
                            config().busVoltage.practicalMaxMillivolts;
                break;
            case Ina2xxAlertFunction::BusVoltageUnder:
                condition = _latestSample.busMillivolts <
                            config().busVoltage.practicalMinMillivolts;
                break;
            case Ina2xxAlertFunction::CurrentOver:
                condition = _latestSample.currentMicroamps >
                            config().current.practicalMaxMicroamps;
                break;
            case Ina2xxAlertFunction::CurrentUnder:
                condition = _latestSample.currentMicroamps <
                            config().current.practicalMinMicroamps;
                break;
            }
        }
        if (!condition) {
            _hardwareAlertCondition = false;
            return;
        }
        if ((maskEnable & ina2xx::ina226AlertFunctionFlag) == 0 ||
            _hardwareAlertCondition) {
            return;
        }

        _hardwareAlertCondition = true;
        _metrics->addAlert();
        if (_alertCallback.has_value()) {
            const Ina2xxAlertEvent event{
                .function = alertConfig.function,
                .sample = _latestSample,
            };
            (*_alertCallback)(event);
        }
    }

    void _recordAge(uint32_t nowMs) const {
        _metrics->recordAge(_hasSample ? static_cast<uint32_t>(
                                             nowMs - _latestSample.capturedAtMs)
                                       : UINT32_MAX);
    }

    [[nodiscard]] uint16_t _ina219Configuration() const {
        constexpr uint16_t gainMask = 0x1800;
        return static_cast<uint16_t>(
            (ina2xx::ina219DefaultConfiguration & ~gainMask) |
            (ina2xx::ina219_gain(_expectedShuntMicrovolts) << 11U));
    }

    std::expected<uint16_t, ReturnCode> _readRegister(ina2xx::Register reg) {
        const std::array<uint8_t, 1> command{{static_cast<uint8_t>(reg)}};
        std::array<uint8_t, 2> payload{};
        auto ret = _device.writeRead(command, payload);
        if (!ret.ok()) {
            return std::unexpected(ret);
        }
        return static_cast<uint16_t>((static_cast<uint16_t>(payload[0]) << 8U) |
                                     payload[1]);
    }

    ReturnCode _writeRegister(ina2xx::Register reg, uint16_t value) {
        const std::array<uint8_t, 3> payload{{
            static_cast<uint8_t>(reg),
            static_cast<uint8_t>(value >> 8U),
            static_cast<uint8_t>(value & 0x00FFU),
        }};
        return _device.write(payload);
    }

    Master &_master;
    Ina2xxModel _model;
    Device _device{};
    ina2xx::Calibration _calibration{};
    uint32_t _expectedShuntMicrovolts = 0;

    Totem::DigitalInput::DigitalInput _alertInput;
    std::atomic<bool> _alertPending{false};
    std::optional<Ina2xxLimitCallback> _limitCallback{};
    std::optional<Ina2xxAlertCallback> _alertCallback{};
    std::optional<Ina2xxSampleCallback> _sampleCallback{};

    Ina2xxSample _latestSample{};
    uint32_t _lastSampleAttemptMs = 0;
    Ina2xxLimitState _busVoltageLimitState = Ina2xxLimitState::Normal;
    Ina2xxLimitState _currentLimitState = Ina2xxLimitState::Normal;
    bool _attemptedSample = false;
    bool _hasSample = false;
    bool _hasBusVoltageLimitState = false;
    bool _hasCurrentLimitState = false;
    bool _hardwareAlertCondition = false;

    std::array<char, MetricConfig::maxMetricGroupNameLength + 1U>
        _metricsName{};
    MetricsBackend::MetricGroupDesc _metricsGroupDesc{
        .name = _metricsName.data(),
        .level = MetricsBackend::MetricLevel::Baseline,
    };
    std::optional<Ina2xxMetrics> _metrics{};
};

inline constexpr LifecycleContract<Ina2xx, Ina2xxConfig>
    _ina2xx_lifecycle_contract;

} // namespace Totem::Wire::I2C::detail
