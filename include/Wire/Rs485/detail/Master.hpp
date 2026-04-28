#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "Macros/Facade.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "Types/Error.hpp"
#include "Wire/Rs485/detail/Config.hpp"
#include "Wire/Rs485/detail/Node.hpp"
#include "Wire/Rs485/detail/Pdu.hpp"
#include "Wire/Rs485/detail/Types.hpp"
#include <atomic>
#include <cstdint>

namespace Totem::Wire::Rs485::detail {

class Master
    : public Node<Master, MasterConfig, TransceiverMode::WriteRead> {
    friend class HasLifecycle<Master, MasterConfig>;
    friend struct LifecycleContract<Master, MasterConfig>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<Master, MasterConfig>;
    friend struct TaskControllerContract<Master>;
    friend struct TaskController::TaskHooks::Contract<Master>;

  public:
    explicit Master(TaskController::IRegistry &registry) : Node(registry) {}

    static constexpr const char *name = "Rs485::Master";
    static constexpr bool sendsHeartbeat = true;
    ReturnCode _onHello(const Header &header) {
        FAIL_IF(header.responseTo != _lastHelloSequence,
                ERR(CoreError, InvalidState),
                "RS485 master received hello for unexpected sequence %u",
                header.responseTo);
        sequence.reset(static_cast<uint8_t>(header.sequenceNumber + 1));
        _state.store(NodeState::Synced, std::memory_order_release);
        _lastHandshakeAttemptMs = 0;
        _deferHeartbeat();
        _log_i("RS485 master handshake complete");
        return OK();
    }

  private:
    ReturnCode _onTaskStep() {
        const auto taskStepStartedAtUs = this->_beginTaskStep();
        if (!ready()) {
            auto ret = _handshakeStep();
            this->_endTaskStep(taskStepStartedAtUs);
            FAIL_IF_ERR_FWD(ret, "Failed RS485 master handshake step");
            return OK();
        }
        auto ret = _runReadyTransactions();
        this->_endTaskStep(taskStepStartedAtUs);
        return ret;
    }

    ReturnCode _handshakeStep() {
        auto state = _state.load(std::memory_order_acquire);
        if (state == NodeState::Initial) {
            sequence.reset();
            FAIL_IF_ERR_FWD(_sendHello(), "Failed to send RS485 hello");
            _lastHandshakeAttemptMs = ::platform::get_time();
            _log_i("RS485 master handshake initiated");
            _state.store(NodeState::HelloSent, std::memory_order_release);
            return OK();
        }

        if (state != NodeState::HelloSent) {
            return OK();
        }

        _log_d("RS485 master waiting for handshake response");
        auto ret = _pollIncoming();
        if (!ret.ok() && ret != ERR(CoreError, NotFound)) {
            return ret;
        }
        const auto nowMs = ::platform::get_time();
        if (!ready() &&
            static_cast<uint32_t>(nowMs - _lastHandshakeAttemptMs) >=
                handshakeRetryMs) {
            _resetTurn();
            FAIL_IF_ERR_FWD(_sendHello(), "Failed to retry RS485 hello");
            _lastHandshakeAttemptMs = nowMs;
            _log_i("RS485 master handshake retry");
        }
        return OK();
    }

    static constexpr uint32_t handshakeRetryMs = 500;
    uint32_t _lastHandshakeAttemptMs = 0;

    static const LogComponent logComponent =
        Totem::Wire::Rs485::detail::logComponent;
};

inline constexpr LifecycleContract<Master, MasterConfig> _master_lifecycle;
inline constexpr TaskControllerContract<Master> _master_task_controller;
inline constexpr TaskController::TaskHooks::Contract<Master> _master_task_hook;

} // namespace Totem::Wire::Rs485::detail
