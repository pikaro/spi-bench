#pragma once

#include "Base/Lifecycle.hh"
#include "Concepts/Base.hh"
#include "Macros/Facade.hh"
#include "Types/Error.hh"
#include <concepts>

namespace Totem::Core {

struct NoConfig {
    // Empty base config

    [[nodiscard]] static bool validate() { return true; }
};

template <class Derived, typename ConfT = NoConfig> class HasLifecycle {
  public:
    ReturnCode begin(const ConfT &cfg = {}) {
        FAIL_IF_NOT(cfg.validate(), ERR(CoreError, InvalidArgument),
                    "Invalid config");
        _log_i("Beginning boot of %s", Derived::name);
        auto ret = _life.begin(derived(), cfg);
        FAIL_IF_ERR(ret, ret, "Failed to begin lifecycle of %s", Derived::name);
        _log_i("Completed boot of %s", Derived::name);
        return OK();
    }

    ReturnCode end() {
        _log_i("Beginning shutdown of %s", Derived::name);
        auto ret = _life.end(derived());
        FAIL_IF_ERR(ret, ret, "Failed to end lifecycle of %s", Derived::name);
        _log_i("Completed shutdown of %s", Derived::name);
        return OK();
    }

    [[nodiscard]] bool active() const { return _life.active(); }

    const ConfT &config() const { return _life.config(); }

  protected:
    Derived &derived() { return static_cast<Derived &>(*this); }

    const Derived &derived() const {
        return static_cast<const Derived &>(*this);
    }

    Lifecycle<Derived, ConfT> _life;

  private:
    friend class Lifecycle<Derived, ConfT>;

    ReturnCode _beginOwner() { return derived()._onBegin(); }
    ReturnCode _endOwner() { return derived()._onEnd(); }

    using DefaultError = LifecycleError;
};

template <class T, typename ConfT = NoConfig> struct LifecycleContract {
    static_assert(IsBeginnable<T>, "Type must be beginnable");
    static_assert(IsEndable<T>, "Type must be endable");
    static_assert(IsNamedEntity<T>, "Type must have a name");
    static_assert(
        requires(const ConfT &cfg) {
            { cfg.validate() } -> std::same_as<bool>;
        }, "Config type must have a validate() method that returns bool");
};

} // namespace Totem::Core

using Totem::Core::HasLifecycle;
using Totem::Core::LifecycleContract;
