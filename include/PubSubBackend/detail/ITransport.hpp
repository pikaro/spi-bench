#pragma once

#include "PubSubBackend/detail/Types.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <limits>
#include <string_view>

namespace Totem::PubSubBackend::detail {

struct ITransportAvailabilityObserver {
    virtual ~ITransportAvailabilityObserver() = default;

    virtual ReturnCode onTransportAvailabilityChanged(TransportId transportId,
                                                      bool available) = 0;
};

struct ITransport {
    virtual ~ITransport() = default;

    virtual ReturnCode send(size_t maxCount = All) = 0;
    virtual ReturnCode receive(size_t maxCount = All) = 0;
    virtual ReturnCode enqueue(FrameHandle frameHandle,
                               const TransportDispatch &dispatch = {}) = 0;
    virtual ReturnCode enqueueRaw(const Header &header,
                                  std::span<const std::byte> frame,
                                  const TransportDispatch &dispatch = {}) = 0;
    virtual ReturnCode
    pollInto(void *ctx, PollIntoCallback callback,
             size_t maxCount = std::numeric_limits<size_t>::max()) = 0;
    [[nodiscard]] virtual TransportId transportId() const = 0;
    [[nodiscard]] virtual std::string_view instanceName() const = 0;
    [[nodiscard]] virtual TransportForwardingPolicy
    forwardingPolicy() const = 0;
    [[nodiscard]] virtual PeerMask knownPeers() const = 0;
    [[nodiscard]] virtual bool available() const = 0;

  private:
    static constexpr size_t All = std::numeric_limits<size_t>::max();
};

} // namespace Totem::PubSubBackend::detail
