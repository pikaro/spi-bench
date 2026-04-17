#pragma once

#include "CommandBackend/Facade.hpp"
#include "Macros/Facade.hpp"

class CommandService {
    inline static Totem::CommandBackend::Controller *controller;

  public:
    /**
     * @brief Installs the command backend used by the service facade.
     * @param ctrl Backend controller instance to expose through CommandService.
     */
    static void setBackend(Totem::CommandBackend::Controller &ctrl) {
        controller = &ctrl;
    }

    static Totem::CommandBackend::Registrar &registrar() {
        ABORT_IF(controller == nullptr, "Command backend controller not set");
        return controller->registrar();
    }
};
