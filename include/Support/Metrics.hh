#pragma once

#include "Common.hh"

#include "Metrics/Facade.hh" // IWYU pragma: export

class Metrics {
  public:
    static Totem::Metrics::Backend &backend() {
        static Totem::Metrics::Backend instance;
        return instance;
    }

    static Totem::Metrics::Registrar &registrar() {
        return backend().registrar();
    }

    static Totem::Metrics::Recorder &recorder() { return backend().recorder(); }

  private:
    using DefaultError = CoreError;
};
