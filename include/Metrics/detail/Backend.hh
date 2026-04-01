#pragma once

#include "Metrics/detail/Recorder.hh"
#include "Metrics/detail/Registrar.hh"
#include "Metrics/detail/Store.hh"
#include "Types/Error.hh"

namespace Totem::Metrics::detail {

class Backend {
  public:
    static constexpr const char *name = "Metrics::System";

    explicit Backend() : _registrar(_store), _recorder(_store) {}

    [[nodiscard]] Registrar &registrar() { return _registrar; }
    [[nodiscard]] Recorder &recorder() { return _recorder; }

  private:
    Store _store;
    Registrar _registrar;
    Recorder _recorder;

    using DefaultError = CoreError;
};

} // namespace Totem::Metrics::detail
