#pragma once

#include "PubSubBackend/Transports/BaseTransport.hpp"
#include <array>
#include <cstddef>
#include <limits>
#include <span>

namespace Totem::PubSubBackend::Transports {

/**
 * Local simulator transport base that publishes received frames directly from
 * the link queue during `pollInto()`.
 *
 * This keeps local-only receive/poll shortcuts out of `BaseTransport`, which
 * remains the shared path for real wire transports.
 */
class LocalPollingTransport : public BaseTransport {
  protected:
    explicit LocalPollingTransport(const BaseTransportDependencies &deps)
        : BaseTransport(deps) {}

    ReturnCode _receiveAvailabilityOnly(size_t maxCount) {
        (void)maxCount;
        FAIL_IF_INACTIVE_ERR("Cannot receive with inactive transport %s",
                             _instanceName);
        FAIL_IF_ERR_FWD(_observeAvailability(),
                        "Failed to observe transport availability");
        return OK();
    }

    ReturnCode _pollReceiveCallbackInto(
        void *ctx, detail::PollIntoCallback callback,
        detail::IngressContext ingressContext,
        size_t maxCount = std::numeric_limits<size_t>::max()) {
        FAIL_IF_INACTIVE_ERR("Cannot poll inactive transport %s",
                             _instanceName);
        FAIL_IF_ERR_FWD(_observeAvailability(),
                        "Failed to observe transport availability");
        if (!_available()) {
            return OK();
        }

        size_t count = 0;
        while (count < maxCount) {
            std::array<std::byte, bufferSize> receiveBuffer;
            auto receiveResult = _receiveCallback(_transport, receiveBuffer);
            if (!receiveResult) {
                if (receiveResult.error() == ERR(Timeout)) {
                    return OK();
                }
                FAIL(receiveResult.error(),
                     "Failed to poll local transport ingress: " ERR_FMT,
                     ERR_ARG(receiveResult.error()));
            }

            auto frame = std::span<const std::byte>{receiveBuffer.data(),
                                                    *receiveResult};
            if (_ingressDispatchCallback != nullptr) {
                FAIL_IF_UNEXPECTED_FWD(
                    handled,
                    _ingressDispatchCallback(_pubSubNode, frame,
                                             ingressContext),
                    "Failed to dispatch local transport ingress frame");
                if (handled) {
                    ++count;
                    continue;
                }
            }

            FAIL_IF_UNEXPECTED_FWD(envelope, _ingress.storeFrame(frame),
                                   "Failed to store local transport ingress "
                                   "frame");
            if (envelope.has_value()) {
                FAIL_IF_ERR_FWD(callback(ctx, *envelope, ingressContext),
                                "Failed to process local transport ingress "
                                "frame");
            }
            ++count;
        }
        return OK();
    }
};

} // namespace Totem::PubSubBackend::Transports
