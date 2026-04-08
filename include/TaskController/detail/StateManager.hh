#pragma once

#include "TaskController/Interfaces/TaskRuntimeSnapshot.hh"
#include <atomic>

namespace Totem::TaskController::detail {

class StateManager {
  public:
    [[nodiscard]] State state() const {
        return _state.load(std::memory_order_acquire);
    }

    bool tryStart() {
        State expected = State::Stopped;
        return _state.compare_exchange_strong(expected, State::Starting);
    }

    bool tryEnterRunning() {
        State expected = State::Starting;
        return _state.compare_exchange_strong(expected, State::Running);
    }

    void enterStopping() {
        _state.store(State::Stopping, std::memory_order_release);
    }

    void enterStopped() {
        _state.store(State::Stopped, std::memory_order_release);
    }

    [[nodiscard]] bool isRunning() const { return state() == State::Running; }

  private:
    std::atomic<State> _state = State::Stopped;
};

} // namespace Totem::TaskController::detail
