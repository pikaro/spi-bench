#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Support/Math.hpp"
#include "Types/Error.hpp"
#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <expected>
#include <optional>

namespace Totem::Clock::detail {

using DefaultError = ClockError;
static constexpr LogComponent logComponent = LogComponent::Clock;

enum class SyncState : uint8_t {
    Initial,
    SyncSent,
    SyncReceived,
    Calibrating,
    Synced,
};

struct SyncRequest {
    int64_t sentTime;
};

struct SyncResponse {
    int64_t recvTime;
    int64_t responseSentTime;
    int64_t responseRecvTime;
};

struct State {
    [[nodiscard]] bool synced() const {
        return _state.load(std::memory_order_acquire) == SyncState::Synced;
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
        _log_w("Clock is not synced, returning unsynced time to %s",
               ::platform::current_task_name());
        return currentTime;
    }

    [[nodiscard]] uint32_t nowMs() const {
        auto tus = nowUs();
        FAIL_IF(will_overflow_div(tus, 1000), 0,
                "Current time in microseconds would overflow when converted to "
                "milliseconds");
        return static_cast<uint32_t>(tus / 1000);
    }

    [[nodiscard]] std::expected<SyncRequest, ReturnCode> requestSync() {
        FAIL_IF_NOT_STATE_UNEXPECTED(_state, SyncState::Initial,
                                     SyncState::SyncSent,
                                     "Failed to request sync");
        _sentTime = ::platform::get_time_us();
        return SyncRequest{.sentTime = _sentTime};
    }

    ReturnCode receiveSyncResponse(const SyncResponse &response) {
        FAIL_IF_NOT_STATE(_state, SyncState::SyncSent, SyncState::SyncReceived,
                          "Failed to receive sync response");
        _recvTime = ::platform::get_time_us();
        _responseSentTime = response.responseSentTime;
        _responseRecvTime = response.responseRecvTime;
        return OK();
    }

    [[nodiscard]] ReturnCode setDrift() {
        FAIL_IF_NOT_STATE(_state, SyncState::SyncReceived,
                          SyncState::Calibrating,
                          "Failed to set drift and complete sync");

        FAIL_IF(will_overflow_sub(_recvTime, _sentTime),
                ERR(ClockError, DriftOverflow),
                "Drift calculation overflow: recvTime (%" PRId64
                ") - sentTime (%" PRId64 "would overflow",
                _recvTime, _sentTime);
        FAIL_IF(will_overflow_sub(_responseSentTime, _responseRecvTime),
                ERR(ClockError, DriftOverflow),
                "Drift calculation overflow: responseSentTime (%" PRId64
                ") - responseRecvTime (%" PRId64 ") would overflow",
                _responseSentTime, _responseRecvTime);

        auto roundTripTime1 = _recvTime - _sentTime;
        auto roundTripTime2 = _responseSentTime - _responseRecvTime;

        int64_t driftCandidate;

        FAIL_IF_NOT_OPT(
            driftCandidate, safe_add(roundTripTime1, roundTripTime2),
            ERR(ClockError, DriftOverflow),
            "Drift calculation overflow: round trip times would overflow");

        _sentTime = 0;
        _recvTime = 0;
        _responseSentTime = 0;
        _responseRecvTime = 0;

        _drift.store(driftCandidate / 2, std::memory_order_release);
        _state.store(SyncState::Synced, std::memory_order_release);

        return OK();
    }

    void reset() {
        _state.store(SyncState::Initial, std::memory_order_release);
        _drift.store(0, std::memory_order_release);
        _sentTime = 0;
        _recvTime = 0;
        _responseSentTime = 0;
        _responseRecvTime = 0;
    }

  private:
    std::atomic<SyncState> _state = SyncState::Initial;

    std::atomic<int64_t> _drift{0};

    int64_t _sentTime = 0;
    int64_t _recvTime = 0;

    int64_t _responseSentTime = 0;
    int64_t _responseRecvTime = 0;
};

} // namespace Totem::Clock::detail
