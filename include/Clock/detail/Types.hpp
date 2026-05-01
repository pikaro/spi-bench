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
    int64_t markerTimeUs;
};

enum class SyncResponseFlags : uint8_t {
    None = 0,
    Valid = 1 << 0,
};

inline constexpr bool hasFlag(SyncResponseFlags flags,
                              SyncResponseFlags flag) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

struct SyncResponse {
    int64_t driftUs;
    SyncResponseFlags flags;
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

    // Called by the transport layer immediately before sending the sync
    // request. SPI patches the request marker when it prepares the
    // request-specific attention edge, which lets the master calculate drift
    // from that hardware-correlated marker instead of from request/response
    // turnaround time.
    void setMarkerTime(int64_t markerTimeUs) {
        if (markerTimeUs != 0) {
            _markerTimeUs = markerTimeUs;
        }
    }

    [[nodiscard]] std::expected<SyncRequest, ReturnCode> requestSync() {
        if (synced()) {
            _resyncInProgress = true;
            _syncState.reset();
        }
        FAIL_IF_ERR_FWD_UNEXPECTED(_syncState.transitionTo(SyncState::SyncSent),
                                   "Failed to request sync");
        _markerTimeUs = ::platform::get_time_us();
        return SyncRequest{.markerTimeUs = _markerTimeUs};
    }

    ReturnCode receiveSyncResponse(const SyncResponse &response) {
        FAIL_IF_ERR_FWD(_syncState.transitionTo(SyncState::SyncReceived),
                        "Failed to receive sync response");
        _pendingDriftUs = response.driftUs;
        return OK();
    }

    [[nodiscard]] ReturnCode setDrift() {
        FAIL_IF_ERR_FWD(_syncState.transitionTo(SyncState::Calibrating),
                        "Failed to set drift and complete sync");

        auto newDrift = _pendingDriftUs;
        _markerTimeUs = 0;
        _pendingDriftUs = 0;

        auto oldDrift = _drift.exchange(newDrift, std::memory_order_release);
        FAIL_IF_ERR_FWD(_syncState.transitionTo(SyncState::Synced),
                        "Failed to mark clock synced");
        _resyncInProgress = false;

        FAIL_IF_ERR_FWD(_onSyncComplete(_owner),
                        "Failed to run on sync complete callback");

        if (oldDrift != 0) {
            if (!will_overflow_sub(newDrift, oldDrift)) {
                _log_i("Clock resynced with drift delta of %" PRId64
                       " us; drift=%" PRId64 " us",
                       newDrift - oldDrift, newDrift);
            } else {
                _log_i("Clock resynced; drift=%" PRId64 " us", newDrift);
            }
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
        _markerTimeUs = 0;
        _pendingDriftUs = 0;
    }

  private:
    StateMachine<SyncState, SyncEvent, syncStateTransitions> _syncState{
        "Clock::State", SyncState::Initial, SyncState::Synced};

    std::atomic<int64_t> _drift{0};

    int64_t _markerTimeUs = 0;
    int64_t _pendingDriftUs = 0;

    void *_owner;
    ReturnCode (*_onSyncComplete)(void *) = nullptr;
    bool _resyncInProgress = false;
};

} // namespace Totem::Clock::detail
