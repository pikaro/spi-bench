#pragma once

#include "Clock/detail/Master.hpp"
#include "Clock/detail/Slave.hpp"
#include "Clock/detail/Types.hpp"
#include "Platform/PlatformSelect.hpp"
#include <cstdint>

namespace Totem::Clock::detail {

class Clock {
  public:
    enum class Role : uint8_t {
        Master,
        Slave,
    };

    explicit Clock(Role role) : _role(role), _slave(_state) {}

    [[nodiscard]] int64_t nowUs() const {
        if (_role == Role::Master) {
            return ::platform::get_time_us();
        }
        return _state.nowUs();
    }

    [[nodiscard]] uint32_t nowMs() const {
        if (_role == Role::Master) {
            return ::platform::get_time();
        }
        return _state.nowMs();
    }

  private:
    Role _role;
    State _state{};
    Master _master;
    Slave _slave;
};

} // namespace Totem::Clock::detail
