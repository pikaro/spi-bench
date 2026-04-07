#pragma once

#include "CommandBackend/Facade.hh"
#include "Macros/Facade.hh"
#include "Types/Error.hh"

class CommandService {
    inline static Totem::CommandBackend::Controller *controller;

  public:
    static void setBackend(Totem::CommandBackend::Controller &ctrl) {
        controller = &ctrl;
    }

    static Totem::CommandBackend::Registrar &registrar() {
        ABORT_IF(controller == nullptr, "Command backend controller not set");
        return controller->registrar();
    }

  private:
    using DefaultError = CoreError;
};
