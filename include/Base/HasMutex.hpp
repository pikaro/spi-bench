#pragma once

#include "Concepts/Base.hpp"
#include "Macros/Facade.hpp"
#include "Mutex/Facade.hpp"
#include <cstdint>
#include <optional>

namespace Totem::Core {

constexpr uint32_t globalDefaultMutexTimeoutMs = 100;

template <class T> struct MutexContract;

template <class Derived> class HasMutex {
  public:
    HasMutex() : _name{Derived::name} {}

  protected:
    [[nodiscard]] Totem::Mutex::ScopedMutexGuard<Derived>
    _mutexGuard(std::optional<uint32_t> timeoutMs = std::nullopt) const {
        auto *mtx = _mutex.get();
        ABORT_IF_NULL(mtx, "Mutex for %s cannot be null", _name);
        return Totem::Mutex::ScopedMutexGuard<Derived>{
            mtx,
            ::platform::ms_to_ticks(timeoutMs.value_or(_defaultMutexTimeoutMs)),
        };
    }

    uint32_t _defaultMutexTimeoutMs = globalDefaultMutexTimeoutMs;

  private:
    const char *_name;
    Mutex::Mutex _mutex;
};

template <class T> struct MutexContract {
    static_assert(IsNamedEntity<T>, "Type must have a name");
};

} // namespace Totem::Core

using Totem::Core::HasMutex;
using Totem::Core::MutexContract;
