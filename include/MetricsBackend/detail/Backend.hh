#pragma once

#include "MetricsBackend/detail/Recorder.hh"
#include "MetricsBackend/detail/Registrar.hh"
#include "MetricsBackend/detail/Store.hh"
#include "Types/Error.hh"

namespace Totem::MetricsBackend::detail {

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

} // namespace Totem::MetricsBackend::detail
