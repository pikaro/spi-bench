#pragma once

#include "Data.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <type_traits>

namespace Totem::PubSubBackend::detail {

using Spec = NodeData::PubSub;

using MessageId = uint32_t;

using TopicMask = TopicId;
using TransportId = uint8_t;
using TransportMask = TransportId;
using PeerMask = PeerId;

/**
 * Describes how a transport instance may redistribute messages at the PubSub
 * layer.
 */
enum class TransportForwardingPolicy : uint8_t {
    PointToPoint = 0,
    SharedBusEdge,
    SharedBusRouter,
};

/**
 * Identifies where a message entered the local PubSub node.
 *
 * `peerId` is intentionally optional so point-to-point transports can provide
 * only a transport identity until the peer-aware shared-bus implementation is
 * introduced.
 */
struct IngressContext {
    TransportId transportId = 0;
    PeerId peerId = 0;

    [[nodiscard]] bool valid() const { return transportId != 0; }
    [[nodiscard]] bool hasPeer() const { return peerId != 0; }
};

/**
 * Describes how the PubSub core wants a transport instance to fan a message
 * out. Point-to-point and edge transports can ignore `targetPeers`.
 */
struct TransportDispatch {
    std::optional<IngressContext> ingress;
    PeerMask targetPeers = 0;

    [[nodiscard]] bool hasIngress() const { return ingress.has_value(); }
    [[nodiscard]] bool hasTargetPeers() const { return targetPeers != 0; }
};

inline constexpr size_t maxPeerCount =
    std::numeric_limits<std::underlying_type_t<typename Spec::NodeId>>::digits;

[[nodiscard]] constexpr std::expected<size_t, ReturnCode>
peerIndex(PeerId peerId) {
    FAIL_IF(peerId == 0, std::unexpected(ERR(InvalidArgument)),
            "Peer ID cannot be 0");
    FAIL_IF(std::popcount(peerId) != 1, std::unexpected(ERR(InvalidArgument)),
            "Peer ID must be a one-hot bitmask");
    return static_cast<size_t>(std::countr_zero(peerId));
}

struct StoredFrame {
    Envelope envelope{};
    TransportMask pendingMask = 0;
    uint8_t pendingCount = 0;

    [[nodiscard]] bool valid() const {
        return pendingCount > 0 ? pendingMask != 0 : true;
    }
};

using FrameHandle = StoredFrame *;

using PollIntoCallback = ReturnCode (*)(void *ctx, const Envelope &envelope,
                                        std::optional<IngressContext> ingress);
using IngressDispatchCallback =
    std::expected<bool, ReturnCode> (*)(void *ctx,
                                        std::span<const std::byte> frame,
                                        std::optional<IngressContext> ingress);
using PublishCallback = ReturnCode (*)(void *ctx, const Envelope &envelope);

inline constexpr LogComponent logComponent = LogComponent::PubSub;

} // namespace Totem::PubSubBackend::detail
