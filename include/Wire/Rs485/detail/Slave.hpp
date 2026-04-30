#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "Macros/Facade.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "Types/Error.hpp"
#include "Wire/Rs485/Interfaces/SlaveConfig.hpp"
#include "Wire/Rs485/detail/Node.hpp"
#include "Wire/Rs485/detail/Pdu.hpp"
#include "Wire/Rs485/detail/Types.hpp"
#include <atomic>
#include <cstdint>

namespace Totem::Wire::Rs485::detail {

class Slave : public Node<Slave, SlaveConfig, TransceiverMode::ReadWrite> {
    friend class HasLifecycle<Slave, SlaveConfig>;
    friend struct LifecycleContract<Slave, SlaveConfig>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<Slave, SlaveConfig>;
    friend struct TaskControllerContract<Slave>;
    friend struct TaskController::TaskHooks::Contract<Slave>;

  public:
    explicit Slave(TaskController::IRegistry &registry) : Node(registry) {}

    static constexpr const char *name = "Rs485::Slave";
    static constexpr bool sendsHeartbeat = false;
    ReturnCode _onHello(const Header &header) {
        sequence.reset(static_cast<uint8_t>(header.sequenceNumber + 1));
        FAIL_IF_ERR_FWD(_sendHello(header.sequenceNumber, FrameTurn::Reaction),
                        "Failed to send RS485 slave hello response");
        _state.store(NodeState::Synced, std::memory_order_release);
        _deferHeartbeat();
        _log_i("RS485 slave handshake complete");
        return OK();
    }

  private:
    ReturnCode _onTaskStep() {
        const auto taskStepStartedAtUs = this->_beginTaskStep();
        if (!ready()) {
            auto ret = _pollIncoming();
            this->_endTaskStep(taskStepStartedAtUs);
            if (!ret.ok() && ret != ERR(CoreError, NotFound)) {
                return ret;
            }
            return OK();
        }
        auto ret = _runReadyTransactions();
        this->_endTaskStep(taskStepStartedAtUs);
        return ret;
    }

    static constexpr LogComponent logComponent =
        Totem::Wire::Rs485::detail::logComponent;
};

inline constexpr LifecycleContract<Slave, SlaveConfig> _slave_lifecycle;
inline constexpr TaskControllerContract<Slave> _slave_task_controller;
inline constexpr TaskController::TaskHooks::Contract<Slave> _slave_task_hook;

} // namespace Totem::Wire::Rs485::detail
