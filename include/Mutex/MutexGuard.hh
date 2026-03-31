#pragma once

#include "freertos/idf_additions.h"

namespace Totem::Core {

class MutexGuard {
  public:
    MutexGuard(SemaphoreHandle_t mtx, TickType_t timeout = portMAX_DELAY)
        : _mtx(mtx), _locked(xSemaphoreTake(mtx, timeout) == pdTRUE) {}
    ~MutexGuard() {
        if (_locked) {
            xSemaphoreGive(_mtx);
        }
    }
    [[nodiscard]] bool locked() const { return _locked; }

  private:
    SemaphoreHandle_t _mtx{};
    bool _locked{};
};

} // namespace Totem::Core
