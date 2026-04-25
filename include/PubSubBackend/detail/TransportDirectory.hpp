#pragma once

#include "Generic/Directory.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/detail/ITransport.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstring>
#include <expected>
#include <string_view>

namespace Totem::PubSubBackend::detail {

struct TransporterEntry {
    TransportId transportId = 0;
    ITransport *transporter{};
    TopicMask topicMask = static_cast<TopicMask>(Spec::Topic::PubSub);
    std::array<TopicMask, maxPeerCount> peerTopicMasks{};
    TransportForwardingPolicy forwardingPolicy =
        TransportForwardingPolicy::PointToPoint;
    PeerMask knownPeerMask = 0;
    std::string_view name;
};

class TransportDirectory;

using TransporterDirectoryImpl =
    BaseDirectory<TransportDirectory, TransportId, TransporterEntry,
                  Spec::Limits::maxTransports>;

class TransportDirectory : public TransporterDirectoryImpl {
    using Base = TransporterDirectoryImpl;

  public:
    struct Snapshot {
        std::array<TransporterEntry, Spec::Limits::maxTransports> entries{};
        size_t count = 0;
    };

    explicit TransportDirectory(const char *ownerName)
        : Base(ownerName, Totem::PubSubBackend::detail::logComponent) {}

    std::expected<TransportId, ReturnCode> add(TransportId transportId,
                                               std::string_view transporterName,
                                               ITransport &transporter) {
        FAIL_IF(this->hasTransport(transportId),
                std::unexpected(ERR(AlreadyExists)),
                "Transport with ID " SV_FMT " already exists in %s",
                MAGIC_SV_ARG(Spec::Transport, transportId), this->ownerName());
        auto entry = TransporterEntry{
            .transportId = transportId,
            .transporter = &transporter,
            .topicMask = static_cast<TopicMask>(Spec::Topic::PubSub),
            .forwardingPolicy = transporter.forwardingPolicy(),
            .knownPeerMask = transporter.knownPeers(),
            .name = transporterName,
        };
        _log_i("%s: add transport " SV_FMT " (%u) with topic mask 0x%02x"
               " and forwarding policy %u",
               this->ownerName(), SV_ARG(entry.name), entry.transportId,
               static_cast<unsigned>(entry.topicMask),
               static_cast<unsigned>(entry.forwardingPolicy));
        return _addImpl(transportId, entry);
    }

    ReturnCode subscribeTransport(const IngressContext &ingress,
                                  TopicMask topicMask) {
        return _withEntryId(ingress.transportId, [this, &ingress, &topicMask](
                                                     const TransportId &,
                                                     TransporterEntry &entry) {
            if (entry.forwardingPolicy ==
                    TransportForwardingPolicy::SharedBusRouter &&
                ingress.hasPeer()) {
                auto peerIndexResult = peerIndex(ingress.peerId);
                if (!peerIndexResult) {
                    return peerIndexResult.error();
                }
                const auto newPeerBits =
                    topicMask & ~entry.peerTopicMasks[*peerIndexResult];
                if (newPeerBits == 0) {
                    return OK();
                }
                _log_i("%s: subscribe transport %u peer %u to topic mask 0x%02x",
                       this->ownerName(), ingress.transportId, ingress.peerId,
                       static_cast<unsigned>(newPeerBits));
                entry.knownPeerMask |= ingress.peerId;
                entry.peerTopicMasks[*peerIndexResult] |= topicMask;
                entry.topicMask |= topicMask;
                return OK();
            }
            const auto newBits = topicMask & ~entry.topicMask;
            if (newBits == 0) {
                return OK();
            }
            _log_i("%s: subscribe transport %u peer %u to topic mask 0x%02x",
                   this->ownerName(), ingress.transportId, ingress.peerId,
                   static_cast<unsigned>(newBits));
            entry.topicMask |= topicMask;
            return OK();
        });
    }

    ReturnCode unsubscribeTransport(const IngressContext &ingress,
                                    TopicMask topicMask) {
        _log_i("%s: unsubscribe transport %u peer %u from topic mask 0x%02x",
               this->ownerName(), ingress.transportId, ingress.peerId,
               static_cast<unsigned>(topicMask));
        return _withEntryId(ingress.transportId, [&ingress, &topicMask](
                                                     const TransportId &,
                                                     TransporterEntry &entry) {
            if (entry.forwardingPolicy ==
                    TransportForwardingPolicy::SharedBusRouter &&
                ingress.hasPeer()) {
                auto peerIndexResult = peerIndex(ingress.peerId);
                if (!peerIndexResult) {
                    return peerIndexResult.error();
                }
                entry.peerTopicMasks[*peerIndexResult] &= ~topicMask;

                auto aggregateMask =
                    static_cast<TopicMask>(Spec::Topic::PubSub);
                for (const auto peerMask : entry.peerTopicMasks) {
                    aggregateMask |= peerMask;
                }
                entry.topicMask = aggregateMask;
                return OK();
            }

            entry.topicMask &= ~topicMask;
            return OK();
        });
    }

    [[nodiscard]] bool hasTransport(TransportId transportId) const {
        FAIL_IF(transportId == 0, false, "Invalid transport ID: 0");
        return any(
            [transportId](const TransportId &, const TransporterEntry &entry) {
                return entry.transportId == transportId;
            });
    }

    template <typename Filter>
        requires(std::is_invocable_r_v<bool, Filter, const TransportId &,
                                       const TransporterEntry &>)
    [[nodiscard]] std::expected<Snapshot, ReturnCode>
    snapshot(Filter &&filter) const {
        Snapshot out{};
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            keys, this->snapshotKeys(std::forward<Filter>(filter)),
            "Failed to snapshot transport keys for %s", this->ownerName());

        for (size_t i = 0; i < keys.count; ++i) {
            FAIL_IF_ERR_FWD_UNEXPECTED(
                this->withEntryConst(
                    keys.keys[i],
                    [&](const TransporterEntry &entry) {
                        auto snapshotEntry = entry;
                        snapshotEntry.knownPeerMask =
                            snapshotEntry.transporter->knownPeers();
                        out.entries[out.count++] = snapshotEntry;
                        return OK();
                    }),
                "Failed to snapshot transport entry %u for %s",
                static_cast<unsigned>(keys.keys[i]), this->ownerName());
        }

        return out;
    }

    [[nodiscard]] std::expected<Snapshot, ReturnCode> snapshot() const {
        return snapshot(
            [](const TransportId &, const TransporterEntry &) { return true; });
    }

  private:
    template <typename Fn>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const TransportId &,
                                       TransporterEntry &>)
    ReturnCode _withEntryId(TransportId transportId, Fn &&fn) {
        FAIL_IF(transportId == 0, ERR(InvalidArgument),
                "Invalid transport ID: 0");
        auto fnRef = std::ref(fn);
        auto filter = [&transportId](const TransportId &,
                                     const TransporterEntry &entry) {
            return entry.transportId == transportId;
        };
        return this->withAll(fnRef, std::move(filter));
    }
};

} // namespace Totem::PubSubBackend::detail
