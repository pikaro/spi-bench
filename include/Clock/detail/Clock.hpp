#pragma once

#include "Clock/detail/Master.hpp"
#include "Clock/detail/Slave.hpp"
#include "Clock/detail/Types.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Services/Clock.hpp"
#include "Types/Error.hpp"
#include "Wire/Interfaces/Request.hpp"
#include <concepts>
#include <cstdint>
#include <expected>
#include <optional>

namespace Totem::Clock::detail {

class Clock : public IClock {
  public:
    enum class Role : uint8_t {
        Master,
        Slave,
    };

    explicit Clock(Role role) : _role(role), _slave(_state) {}

    [[nodiscard]] int64_t nowUs() const override {
        if (_role == Role::Master) {
            return ::platform::get_time_us();
        }
        return _state.nowUs();
    }

    [[nodiscard]] uint32_t nowMs() const override {
        if (_role == Role::Master) {
            return ::platform::get_time();
        }
        return _state.nowMs();
    }

    [[nodiscard]] bool synced() const override {
        return _role == Role::Master || _state.synced();
    }

    [[nodiscard]] bool syncing() const {
        return _role == Role::Slave && _state.syncing();
    }

    [[nodiscard]] std::optional<int64_t> drift() const override {
        if (_role == Role::Master) {
            return 0;
        }
        return _state.drift();
    }

    template <class Link> ReturnCode sync(Link &link) {
        FAIL_IF(_role != Role::Slave, ERR(CoreError, InvalidState),
                "Only a clock slave can initiate clock sync");
        return _slave.sync(link);
    }

    [[nodiscard]] std::expected<Totem::Wire::FrameHandler, ReturnCode>
    handler() {
        FAIL_IF(_role != Role::Master,
                std::unexpected(ERR(CoreError, InvalidState)),
                "Only a clock master can answer clock sync requests");
        return _master.handler();
    }

    template <class Link>
        requires requires(Link link, Totem::Wire::FrameHandler handler) {
            { link.registerHandler(handler) } -> std::same_as<ReturnCode>;
        }
    ReturnCode registerHandler(Link &link) {
        FAIL_IF(_role != Role::Master, ERR(CoreError, InvalidState),
                "Only a clock master can register a clock sync handler");
        return link.registerHandler(_master.handler());
    }

  private:
    static ReturnCode _onSync(void *owner) {
        auto *self = static_cast<Clock *>(owner);
        return self->_onSync();
    }

    ReturnCode _onSync() {
        FAIL_IF(_role == Role::Master, ERR(CoreError, InvalidState),
                "Only a slave clock can be synced");
        ClockService::set(*this);
        return OK();
    }

    Role _role;
    State _state{this, _onSync};
    Master _master;
    Slave _slave;
};

} // namespace Totem::Clock::detail
