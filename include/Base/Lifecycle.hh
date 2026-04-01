#pragma once

#include "Macros/Facade.hh"
#include "Mutex/Mutex.hh"
#include "Mutex/ScopedMutexGuard.hh"
#include "Types/Error.hh"

namespace Totem::Core {

template <class Owner, class ConfT> class Lifecycle {
  public:
    ReturnCode begin(Owner &owner, const ConfT &cfg) {
        ScopedMutexGuard<Owner> guard{_mtx.get()};
        if (_active) {
            return ERR(Active);
        }
        _config = cfg;
        FAIL_IF_ERR(owner._beginOwner(), ERR(CoreError, OperationFailed),
                    "Failed to begin owner-specific lifecycle for %s",
                    Owner::name);
        _active = true;
        return OK();
    }

    ReturnCode end(Owner &owner) {
        ScopedMutexGuard<Owner> guard{_mtx.get()};
        if (!_active) {
            return ERR(NotActive);
        }
        FAIL_IF_ERR(owner._endOwner(), ERR(CoreError, OperationFailed),
                    "Failed to begin owner-specific lifecycle for %s",
                    Owner::name);
        _active = false;
        return OK();
    }

    [[nodiscard]] bool active() const { return _active; }
    const ConfT &config() const { return _config; }

  private:
    Mutex _mtx;
    bool _active{false};
    ConfT _config{};
    using DefaultError = LifecycleError;
};

} // namespace Totem::Core
