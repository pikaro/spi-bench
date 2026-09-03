#pragma once

#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "Services/StatusLed.hpp"
#include "StaticConfig/StatusLed.hpp"
#include "StatusLed/Interfaces/Config.hpp"
#include "StatusLed/Interfaces/Types.hpp"
#include "StatusLed/detail/PlatformSelect.hpp"
#include "StatusLed/detail/Types.hpp"
#include "Types/Error.hpp"
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>

namespace Totem::StatusLed::detail {

class StatusLed : public HasLifecycle<StatusLed, Config>,
                  public Totem::StatusLed::detail::IService {
    friend class HasLifecycle<StatusLed, Config>;
    friend struct LifecycleContract<StatusLed, Config>;

  public:
    DELETE_COPY(StatusLed)
    DELETE_MOVE(StatusLed)

    StatusLed() = default;

    static constexpr const char *name = "StatusLed";
    static constexpr LogComponent logComponent =
        Totem::StatusLed::detail::logComponent;

    Totem::StatusLed::Directory directory() override {
        return Totem::StatusLed::Directory{
            .ctx = this,
            .registerStateFn = StatusLed::registerStateHook,
        };
    }

    ReturnCode setCoreReady() override {
        if (!this->active()) {
            return OK();
        }
        FAIL_IF_ERR_FWD(_setInfoState(_coreReady),
                        "Failed to set core-ready status LED state");
        return _refreshImmediate(::platform::get_time());
    }

    ReturnCode setTargetsReady() override {
        if (!this->active()) {
            return OK();
        }
        FAIL_IF_ERR_FWD(_setInfoState(_targetsReady),
                        "Failed to set targets-ready status LED state");
        return _refreshImmediate(::platform::get_time());
    }

    ReturnCode setOff() override {
        if (!this->active()) {
            return OK();
        }
        FAIL_IF_ERR_FWD(_setInfoState(_off),
                        "Failed to set off status LED state");
        return _refreshImmediate(::platform::get_time());
    }

    ReturnCode recordUnhandledError() override {
        if (!this->active()) {
            return OK();
        }
        if (_unhandledError.valid()) {
            return _setState(_unhandledError.id, true);
        }
        return OK();
    }

    ReturnCode recordCritical() override {
        if (!this->active()) {
            return OK();
        }
        if (_abortCritical.valid()) {
            FAIL_IF_ERR_FWD(_setState(_abortCritical.id, true),
                            "Failed to set abort status LED state");
        }
        return _refreshImmediate(::platform::get_time());
    }

    ReturnCode work(uint32_t nowMs) override {
        if (!this->active() || !config().configured) {
            return OK();
        }

        const bool dirty = _dirty.exchange(false, std::memory_order_acq_rel);
        if (!dirty && !timeReached(nowMs, _nextCycleMs)) {
            return OK();
        }
        return _refresh(nowMs, timeReached(nowMs, _nextCycleMs));
    }

  private:
    struct StateSlot {
        bool used = false;
        StateDef def{};
    };

    static constexpr RgbColor bootingColor{.red = 0, .green = 128, .blue = 128};
    static constexpr RgbColor coreReadyColor{.red = 0, .green = 0, .blue = 160};
    static constexpr RgbColor targetsReadyColor{.red = 0,
                                                .green = 160,
                                                .blue = 0};
    static constexpr RgbColor offColor{};
    static constexpr RgbColor unhandledErrorColor{.red = 180,
                                                   .green = 0,
                                                   .blue = 0};
    static constexpr RgbColor abortColor{.red = 180,
                                         .green = 0,
                                         .blue = 180};
    static constexpr uint8_t invalidState = 0xFF;

    ReturnCode _onBegin() {
        if (config().configured) {
            FAIL_IF_ERR_FWD(_output.begin(config()),
                            "Failed to begin status LED output");
        }
        FAIL_IF_ERR_FWD(_registerPredefinedStates(),
                        "Failed to register predefined status LED states");
        FAIL_IF_ERR_FWD(_setInfoState(_booting),
                        "Failed to set booting status LED state");
        return _refreshImmediate(::platform::get_time());
    }

    ReturnCode _onEnd() {
        auto ret = OK();
        if (config().configured) {
            ret.combine(_output.deinit());
        }
        _clearState();
        return ret;
    }

    std::expected<StateHandle, ReturnCode> _registerState(StateDef def) {
        FAIL_IF_NOT(def.validate(), std::unexpected(ERR(InvalidArgument)),
                    "Invalid status LED state");
        FAIL_IF(_stateCount >= _states.size(),
                std::unexpected(ERR(OutOfMemory)),
                "No available status LED state slots");
        FAIL_IF(_colorExists(def.color), std::unexpected(ERR(AlreadyExists)),
                "Status LED color already registered");

        const auto id = _stateCount++;
        _states[id] = StateSlot{.used = true, .def = def};
        return StateHandle{
            .ctx = this,
            .id = static_cast<uint8_t>(id),
            .setState = StatusLed::setStateHook,
        };
    }

    ReturnCode _registerPredefinedStates() {
        FAIL_IF_UNEXPECTED_FWD(
            booting,
            _registerState({.name = "Booting",
                            .color = bootingColor,
                            .kind = StateKind::Informational}),
            "Failed to register booting status LED state");
        _booting = booting;
        FAIL_IF_UNEXPECTED_FWD(
            coreReady,
            _registerState({.name = "CoreReady",
                            .color = coreReadyColor,
                            .kind = StateKind::Informational}),
            "Failed to register core-ready status LED state");
        _coreReady = coreReady;
        FAIL_IF_UNEXPECTED_FWD(
            targetsReady,
            _registerState({.name = "TargetsReady",
                            .color = targetsReadyColor,
                            .kind = StateKind::Informational}),
            "Failed to register targets-ready status LED state");
        _targetsReady = targetsReady;
        FAIL_IF_UNEXPECTED_FWD(
            off,
            _registerState({.name = "Off",
                            .color = offColor,
                            .kind = StateKind::Informational}),
            "Failed to register off status LED state");
        _off = off;
        FAIL_IF_UNEXPECTED_FWD(
            unhandledError,
            _registerState({.name = "UnhandledError",
                            .color = unhandledErrorColor,
                            .kind = StateKind::Error}),
            "Failed to register unhandled-error status LED state");
        _unhandledError = unhandledError;
        FAIL_IF_UNEXPECTED_FWD(
            abortCritical,
            _registerState({.name = "Abort",
                            .color = abortColor,
                            .kind = StateKind::Critical}),
            "Failed to register abort status LED state");
        _abortCritical = abortCritical;
        return OK();
    }

    bool _colorExists(RgbColor color) const {
        for (std::size_t i = 0; i < _stateCount; ++i) {
            if (_states[i].used && _states[i].def.color == color) {
                return true;
            }
        }
        return false;
    }

    ReturnCode _setInfoState(StateHandle handle) {
        FAIL_IF_NOT(handle.valid(), ERR(InvalidArgument),
                    "Invalid informational status LED handle");
        _activeMasks[kindIndex(StateKind::Informational)].store(
            bitFor(handle.id), std::memory_order_release);
        _dirty.store(true, std::memory_order_release);
        return OK();
    }

    ReturnCode _setState(uint8_t id, bool active) {
        FAIL_IF(id >= _stateCount || !_states[id].used, ERR(InvalidArgument),
                "Invalid status LED state id %u", id);
        const auto index = kindIndex(_states[id].def.kind);
        const auto bit = bitFor(id);
        if (active) {
            if (_states[id].def.kind == StateKind::Informational) {
                _activeMasks[index].store(bit, std::memory_order_release);
            } else {
                _activeMasks[index].fetch_or(bit, std::memory_order_acq_rel);
            }
        } else {
            _activeMasks[index].fetch_and(~bit, std::memory_order_acq_rel);
        }
        _dirty.store(true, std::memory_order_release);
        return OK();
    }

    ReturnCode _refreshImmediate(uint32_t nowMs) {
        _dirty.store(false, std::memory_order_release);
        return _refresh(nowMs, true);
    }

    ReturnCode _refresh(uint32_t nowMs, bool advanceCycle) {
        if (!config().configured) {
            return OK();
        }
        const auto selected = _selectState(advanceCycle);
        if (selected == invalidState) {
            return OK();
        }
        _nextCycleMs = nowMs + StatusLedConfig::cycleIntervalMs;
        if (_lastDisplayed == selected) {
            return OK();
        }

        const auto color =
            config().brightness.apply(_states[selected].def.color);
        FAIL_IF_ERR_FWD(_output.show(color),
                        "Failed to show status LED state %s",
                        _states[selected].def.name);
        _lastDisplayed = selected;
        return OK();
    }

    uint8_t _selectState(bool advanceCycle) {
        for (auto kind : {StateKind::Critical, StateKind::Error,
                          StateKind::Warning, StateKind::Informational}) {
            const auto index = kindIndex(kind);
            const auto mask =
                _activeMasks[index].load(std::memory_order_acquire);
            if (mask == 0) {
                continue;
            }

            if (kind == StateKind::Informational) {
                const auto selected = firstSet(mask);
                _cycleState[index] = selected;
                return selected;
            }

            const auto current = _cycleState[index];
            if (!advanceCycle && current != invalidState &&
                (mask & bitFor(current)) != 0) {
                return current;
            }

            const auto selected = nextSet(mask, current);
            _cycleState[index] = selected;
            return selected;
        }
        return invalidState;
    }

    static uint8_t firstSet(uint32_t mask) {
        return static_cast<uint8_t>(std::countr_zero(mask));
    }

    static uint8_t nextSet(uint32_t mask, uint8_t current) {
        if (current == invalidState) {
            return firstSet(mask);
        }
        if (current >= 31) {
            return firstSet(mask);
        }
        const auto afterCurrent =
            mask & (~uint32_t{0} << static_cast<uint32_t>(current + 1U));
        if (afterCurrent != 0) {
            return firstSet(afterCurrent);
        }
        return firstSet(mask);
    }

    static bool timeReached(uint32_t nowMs, uint32_t deadlineMs) {
        return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
    }

    static constexpr uint32_t bitFor(uint8_t id) {
        return uint32_t{1} << id;
    }

    static constexpr std::size_t kindIndex(StateKind kind) {
        return static_cast<std::size_t>(kind);
    }

    static std::expected<StateHandle, ReturnCode>
    registerStateHook(void *ctx, StateDef def) {
        auto *self = static_cast<StatusLed *>(ctx);
        FAIL_IF_NULL(self, std::unexpected(ERR(InvalidArgument)),
                     "Status LED directory owner is null");
        return self->_registerState(def);
    }

    static ReturnCode setStateHook(void *ctx, uint8_t id, bool active) {
        auto *self = static_cast<StatusLed *>(ctx);
        FAIL_IF_NULL(self, ERR(InvalidArgument),
                     "Status LED state owner is null");
        return self->_setState(id, active);
    }

    void _clearState() {
        _states = {};
        _stateCount = 0;
        for (auto &mask : _activeMasks) {
            mask.store(0, std::memory_order_release);
        }
        _cycleState.fill(invalidState);
        _lastDisplayed = invalidState;
        _dirty.store(false, std::memory_order_release);
        _booting = {};
        _coreReady = {};
        _targetsReady = {};
        _off = {};
        _unhandledError = {};
        _abortCritical = {};
    }

    std::array<StateSlot, StatusLedConfig::maxStates> _states{};
    std::size_t _stateCount = 0;
    std::array<std::atomic<uint32_t>, 4> _activeMasks{};
    std::array<uint8_t, 4> _cycleState{invalidState, invalidState,
                                       invalidState, invalidState};
    std::atomic<bool> _dirty{false};
    uint32_t _nextCycleMs = 0;
    uint8_t _lastDisplayed = invalidState;

    StateHandle _booting{};
    StateHandle _coreReady{};
    StateHandle _targetsReady{};
    StateHandle _off{};
    StateHandle _unhandledError{};
    StateHandle _abortCritical{};

    Platform _output{};
};

inline constexpr LifecycleContract<StatusLed, Config>
    _status_led_lifecycle_contract;

} // namespace Totem::StatusLed::detail
