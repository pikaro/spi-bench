#pragma once

#include "Clock/detail/Types.hpp" // IWYU pragma: keep

namespace Totem::Clock::detail {

class Slave {
  public:
    explicit Slave(State &state) : _state(state) {}

  private:
    State &_state;
};

} // namespace Totem::Clock::detail
