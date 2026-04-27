#pragma once

#include "Base/HasLifecycle.hpp"
#include "Wire/Rs485/detail/Config.hpp"
#include "Wire/Rs485/detail/PlatformSelect.hpp"

namespace Totem::Wire::Rs485::detail {

class Master : public HasLifecycle<Master, MasterConfig> {
  public:
};

} // namespace Totem::Wire::Rs485::detail
