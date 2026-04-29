#pragma once

#include "Generic/StateMachine.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Support/Math.hpp"
#include "Types/Error.hpp"
#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <expected>
#include <optional>

namespace Totem::Clock::detail {

using DefaultError = ClockError;
static constexpr LogComponent logComponent = LogComponent::Clock;

enum class SyncEvent : uint8_t {
    Default,
};

enum class SyncState : uint8_t {
    Invalid,
    Initial,
    SyncSent,
    SyncReceived,
    Calibrating,
    Synced,
};

constexpr std::array syncStateTransitions{
    TRANSITION(Sync, Initial, SyncSent),
    TRANSITION(Sync, SyncSent, SyncReceived),
    TRANSITION(Sync, SyncReceived, Calibrating),
    TRANSITION(Sync, Calibrating, Synced),
};

struct SyncRequest {
    int64_t sentTime;
};

struct SyncResponse {
    int64_t requestReceivedTime;
    int64_t responseSentTime;
};

struct State {
    explicit State(void *owner, ReturnCode (*onSyncComplete)(void *) = nullptr)
        : _owner(owner), _onSyncComplete(onSyncComplete) {}

    [[nodiscard]] bool synced() const {
        return _syncState.is(SyncState::Synced) || _resyncInProgress;
    }

    [[nodiscard]] bool syncing() const {
        return _syncState.is(SyncState::SyncSent) ||
               _syncState.is(SyncState::SyncReceived) ||
               _syncState.is(SyncState::Calibrating);
    }

    [[nodiscard]] std::optional<int64_t> drift() const {
        FAIL_IF_NOT(synced(), std::nullopt, "Cannot get drift when not synced");
        return _drift.load(std::memory_order_acquire);
    }

    [[nodiscard]] int64_t nowUs() const {
        auto currentTime = ::platform::get_time_us();
        if (synced()) {
            auto drift = _drift.load(std::memory_order_acquire);
            FAIL_IF(will_overflow_add(currentTime, drift), currentTime,
                    "Current time and drift would overflow when added");
            return currentTime + drift;
        }
        return currentTime;
    }

    [[nodiscard]] uint32_t nowMs() const {
        auto tus = nowUs();
        FAIL_IF(will_overflow_div(tus, 1000), 0,
                "Current time in microseconds would overflow when converted to "
                "milliseconds");
        return static_cast<uint32_t>(tus / 1000);
    }

    // Called by the transport layer with the time captured just before the
    // request frame was transmitted. Overwrites the coarse T1 from
    // requestSync() with a more accurate wire-level timestamp.
    void setSentTime(int64_t sentAtUs) {
        if (sentAtUs != 0) {
            _sentTime = sentAtUs;
        }
    }

    [[nodiscard]] std::expected<SyncRequest, ReturnCode> requestSync() {
        if (synced()) {
            _resyncInProgress = true;
            _syncState.reset();
        }
        FAIL_IF_ERR_FWD_UNEXPECTED(_syncState.transitionTo(SyncState::SyncSent),
                                   "Failed to request sync");
        _sentTime = ::platform::get_time_us();
        return SyncRequest{.sentTime = _sentTime};
    }

    ReturnCode receiveSyncResponse(const SyncResponse &response) {
        FAIL_IF_ERR_FWD(_syncState.transitionTo(SyncState::SyncReceived),
                        "Failed to receive sync response");
        _recvTime = ::platform::get_time_us();
        _requestReceivedTime = response.requestReceivedTime;
        _responseSentTime = response.responseSentTime;
        return OK();
    }

    [[nodiscard]] ReturnCode setDrift() {
        FAIL_IF_ERR_FWD(_syncState.transitionTo(SyncState::Calibrating),
                        "Failed to set drift and complete sync");

        FAIL_IF(will_overflow_sub(_requestReceivedTime, _sentTime),
                ERR(ClockError, DriftOverflow),
                "Drift calculation overflow: requestReceivedTime (%" PRId64
                ") - sentTime (%" PRId64 "would overflow",
                _requestReceivedTime, _sentTime);
        FAIL_IF(will_overflow_sub(_responseSentTime, _recvTime),
                ERR(ClockError, DriftOverflow),
                "Drift calculation overflow: responseSentTime (%" PRId64
                ") - recvTime (%" PRId64 ") would overflow",
                _responseSentTime, _recvTime);

        auto roundTripTime1 = _requestReceivedTime - _sentTime;
        auto roundTripTime2 = _responseSentTime - _recvTime;

        int64_t driftCandidate;

        FAIL_IF_NOT_OPT(
            driftCandidate, safe_add(roundTripTime1, roundTripTime2),
            ERR(ClockError, DriftOverflow),
            "Drift calculation overflow: round trip times would overflow");

        _sentTime = 0;
        _recvTime = 0;
        _requestReceivedTime = 0;
        _responseSentTime = 0;

        auto newDrift = driftCandidate / 2;
        auto oldDrift = _drift.exchange(newDrift, std::memory_order_release);
        FAIL_IF_ERR_FWD(_syncState.transitionTo(SyncState::Synced),
                        "Failed to mark clock synced");
        _resyncInProgress = false;

        FAIL_IF_ERR_FWD(_onSyncComplete(_owner),
                        "Failed to run on sync complete callback");

        if (oldDrift != 0) {
            _log_i("Clock resynced with drift delta of %" PRId64
                   " us; drift=%" PRId64 " us",
                   newDrift - oldDrift, newDrift);
        }

        return OK();
    }

    void reset() {
        if (_resyncInProgress) {
            _syncState.reset(SyncState::Synced);
            _resyncInProgress = false;
        } else {
            _syncState.reset();
            _drift.store(0, std::memory_order_release);
        }
        _sentTime = 0;
        _recvTime = 0;
        _requestReceivedTime = 0;
        _responseSentTime = 0;
    }

  private:
    StateMachine<SyncState, SyncEvent, syncStateTransitions> _syncState{
        "Clock::State", SyncState::Initial, SyncState::Synced};

    std::atomic<int64_t> _drift{0};

    int64_t _sentTime = 0;
    int64_t _recvTime = 0;

    int64_t _requestReceivedTime = 0;
    int64_t _responseSentTime = 0;

    void *_owner;
    ReturnCode (*_onSyncComplete)(void *) = nullptr;
    bool _resyncInProgress = false;
};

} // namespace Totem::Clock::detail
