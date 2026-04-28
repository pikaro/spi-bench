#pragma once

#include "Clock/detail/Types.hpp" // IWYU pragma: keep
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include "Wire/Interfaces/Request.hpp"
#include <span>

namespace Totem::Clock::detail {

class Slave {
  public:
    explicit Slave(State &state) : _state(state) {}

    template <class Link> ReturnCode sync(Link &link) {
        FAIL_IF_UNEXPECTED_FWD(request, _state.requestSync(),
                               "Failed to create clock sync request");
        _request = request;
        auto wireRequest = Totem::Wire::ExchangeRequest{
            .owner = this,
            .payloadType = Totem::Wire::PayloadType::Clock,
            .request = std::as_bytes(std::span(&_request, 1)),
            .response = std::as_writable_bytes(std::span(&_response, 1)),
            .onComplete = _onSyncComplete,
        };
        return link.exchange(wireRequest);
    }

  private:
    static ReturnCode _onSyncComplete(Totem::Wire::ExchangeResult result) {
        auto *self = static_cast<Slave *>(result.owner);
        if (!result.result.ok()) {
            _log_w("Clock sync exchange failed: " ERR_FMT,
                   ERR_ARG(result.result));
            self->_state.reset();
            return OK();
        }
        if (result.length != sizeof(SyncResponse)) {
            _log_w("Clock sync response length %u did not match expected %zu",
                   result.length, sizeof(SyncResponse));
            self->_state.reset();
            return OK();
        }
        self->_state.setSentTime(result.sentAtUs);
        if (auto ret = self->_state.receiveSyncResponse(self->_response);
            !ret.ok()) {
            _log_w("Failed to receive clock sync response: " ERR_FMT,
                   ERR_ARG(ret));
            self->_state.reset();
            return OK();
        }
        if (auto ret = self->_state.setDrift(); !ret.ok()) {
            _log_w("Failed to apply clock sync response: " ERR_FMT,
                   ERR_ARG(ret));
            self->_state.reset();
            return OK();
        }
        _log_i("Clock sync complete");
        return OK();
    }

    State &_state;
    SyncRequest _request{};
    SyncResponse _response{};
};

} // namespace Totem::Clock::detail
