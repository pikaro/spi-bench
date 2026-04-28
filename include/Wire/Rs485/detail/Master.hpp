#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "Macros/Facade.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "Types/Error.hpp"
#include "Wire/Interfaces/Request.hpp"
#include "Wire/Rs485/detail/Config.hpp"
#include "Wire/Rs485/detail/Node.hpp"
#include "Wire/Rs485/detail/Pdu.hpp"
#include "Wire/Rs485/detail/Types.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace Totem::Wire::Rs485::detail {

class Master : public Node<Master, MasterConfig> {
    friend class HasLifecycle<Master, MasterConfig>;
    friend struct LifecycleContract<Master, MasterConfig>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<Master, MasterConfig>;
    friend struct TaskControllerContract<Master>;
    friend struct TaskController::TaskHooks::Contract<Master>;

  public:
    explicit Master(TaskController::IRegistry &registry) : Node(registry) {}

    static constexpr const char *name = "Rs485::Master";

  private:
    ReturnCode _onTaskStep() { return OK(); }

    ReturnCode _handshake() {
        _log_d("Performing handshake with slave");
        _headerBuf = Header::hello().toBytes();
    }

    static const LogComponent logComponent =
        Totem::Wire::Rs485::detail::logComponent;
};

inline constexpr LifecycleContract<Master, MasterConfig> _master_lifecycle;
inline constexpr TaskControllerContract<Master> _master_task_controller;
inline constexpr TaskController::TaskHooks::Contract<Master> _master_task_hook;

} // namespace Totem::Wire::Rs485::detail
