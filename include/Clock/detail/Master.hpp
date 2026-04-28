#pragma once

#include "Clock/detail/Types.hpp" // IWYU pragma: keep
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Types/Error.hpp"
#include "Wire/Interfaces/Request.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>

namespace Totem::Clock::detail {

class Master {
  public:
    Totem::Wire::FrameHandler handler() {
        return {
            .owner = this,
            .payloadType = Totem::Wire::PayloadType::Clock,
            .response = std::as_writable_bytes(std::span(&_response, 1)),
            .onData = nullptr,
            .onRequest = _onSyncRequest,
        };
    }

  private:
    static std::expected<uint16_t, ReturnCode>
    _onSyncRequest(void *owner, Totem::Wire::PayloadType /*payloadType*/,
                   std::span<const std::byte> request,
                   std::span<std::byte> response, int64_t receivedAtUs) {
        auto *self = static_cast<Master *>(owner);
        FAIL_IF(request.size() != sizeof(SyncRequest),
                std::unexpected(ERR(CoreError, InvalidData)),
                "Invalid clock sync request length %zu", request.size());
        FAIL_IF(response.size() < sizeof(SyncResponse),
                std::unexpected(ERR(CoreError, InvalidArgument)),
                "Clock sync response buffer too small: %zu", response.size());

        SyncRequest syncRequest{};
        std::memcpy(&syncRequest, request.data(), sizeof(syncRequest));
        (void)syncRequest;

        self->_response = {
            .requestReceivedTime = receivedAtUs,
            .responseSentTime = ::platform::get_time_us(),
        };
        return static_cast<uint16_t>(sizeof(SyncResponse));
    }

    SyncResponse _response{};
};

} // namespace Totem::Clock::detail
