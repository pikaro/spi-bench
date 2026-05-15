#pragma once

#include "Macros/Facade.hpp"
#include "PlatformSelect.hpp"
#include <cstdint>

namespace Totem::Mutex::detail {

enum class MutexAllocation : uint8_t {
    Static,
    Dynamic,
};

class Mutex {
  public:
    explicit Mutex(MutexAllocation allocation = MutexAllocation::Static)
        : _handle{Platform::create_mutex(
                      allocation == MutexAllocation::Static ? &_storage
                                                            : nullptr)
                      .value_or(nullptr)} {
        ABORT_IF_NULL(_handle, "Failed to create mutex");
    }

    ~Mutex() {
        if (_handle != nullptr) {
            FAIL_IF_ERR_VOID(Platform::destroy_mutex(_handle),
                             "Failed to destroy mutex");
        }
    }

    Mutex(const Mutex &) = delete;
    Mutex &operator=(const Mutex &) = delete;

    Mutex(Mutex &&) = delete;
    Mutex &operator=(Mutex &&) = delete;

    [[nodiscard]] MutexHandle get() const { return _handle; }

  private:
    Platform::MutexStorage _storage{};
    MutexHandle _handle{};
};

} // namespace Totem::Mutex::detail
