#pragma once

#include "Common.hh"

#include "Base/HasLifecycle.hh"
#include "TaskControllerRegistry/detail/Config.hh"
#include "TaskControllerRegistry/detail/Directory.hh"

namespace Totem::TaskControllerRegistry::detail {

class Registry : public HasLifecycle<Registry, Config> {
    friend class HasLifecycle<Registry, Config>;
    friend struct LifecycleContract<Registry, Config>;

    using ControllerNameKey = Directory::EntryNameKey;

  public:
    // NOTE: Must enable here because TaskController wants to register
    //       itself in its constructor
    Registry() { _enableRegistration(); }

    static constexpr const char *name = "TaskControllerRegistry::Registry";

    ReturnCode registerController(const char *ownerName,
                                  TaskController::Controller *controller) {
        FAIL_IF_NULL(ownerName, ERR(InvalidArgument),
                     "Cannot register controller with null owner name");
        FAIL_IF_NULL(controller, ERR(InvalidArgument),
                     "Cannot register null controller");
        auto nameKey = ControllerNameKey::fromCharPtr(ownerName);
        return registerController(nameKey, controller);
    }

    ReturnCode registerController(ControllerNameKey ownerNameKey,
                                  TaskController::Controller *controller) {
        auto ret = _directory.add(ownerNameKey, controller);
        FAIL_IF(!ret, ret.error(), "Failed to register controller %s",
                ownerNameKey.name.data());
        return OK();
    }

    ReturnCode deregisterController(const char *ownerName) {
        FAIL_IF_NULL(ownerName, ERR(InvalidArgument),
                     "Cannot deregister controller with null owner name");
        auto nameKey = ControllerNameKey::fromCharPtr(ownerName);
        return deregisterController(nameKey);
    }

    ReturnCode deregisterController(ControllerNameKey ownerNameKey) {
        auto ret = _directory.remove(ownerNameKey);
        FAIL_IF_ERR(ret, ret, "Failed to deregister controller %s",
                    ownerNameKey.name.data());
        return OK();
    }

    [[nodiscard]] TaskController::RegistryHooks hooks() {
        return TaskController::RegistryHooks::bind(*this);
    }

  private:
    void _disableRegistration() { _directory.disableRegistration(); }
    void _enableRegistration() { _directory.enableRegistration(); }

    static ReturnCode _onBegin() { return OK(); }
    ReturnCode _onEnd() {
        _disableRegistration();
        return OK();
    }

    Directory _directory;

    using DefaultError = CoreError;
};

inline constexpr LifecycleContract<Registry, Config> _registry_lifecycle;
inline constexpr TaskController::RegistryHooks::Contract<Registry>
    _registry_hooks_contract;

} // namespace Totem::TaskControllerRegistry::detail
