#pragma once

#include "MetricsBackend/Facade.hh" // IWYU pragma: export

class Metrics {
  public:
    static Totem::MetricsBackend::Backend &backend() {
        static Totem::MetricsBackend::Backend instance;
        return instance;
    }

    static Totem::MetricsBackend::Registrar &registrar() {
        return backend().registrar();
    }

    static Totem::MetricsBackend::Recorder &recorder() {
        return backend().recorder();
    }
};
