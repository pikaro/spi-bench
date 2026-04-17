#pragma once

#include "Generic/Directory.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/detail/Transporter.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstring>
#include <expected>
#include <string_view>

namespace Totem::PubSubBackend::detail {

struct TransporterEntry {
    TransportId transportId = 0;
    Transporter transporter{};
    TopicMask topicMask = static_cast<TopicMask>(Spec::Topic::PubSub);
    std::string_view name;
};

using TransporterDirectoryImpl =
    Directory<TransportId, TransporterEntry, Spec::Limits::maxTransports>;

class TransporterDirectory : public TransporterDirectoryImpl {
    using Base = TransporterDirectoryImpl;

  public:
    struct Snapshot {
        std::array<TransporterEntry, Spec::Limits::maxTransports> entries{};
        size_t count = 0;
    };

    explicit TransporterDirectory(const char *ownerName)
        : Base(ownerName, Totem::PubSubBackend::detail::logComponent) {}

    using EntryKey = typename Base::EntryKey;

    std::expected<EntryKey, ReturnCode> add(TransportId transportId,
                                            std::string_view transporterName,
                                            Transporter transporter) {
        FAIL_IF_NOT(transporter.validate(),
                    std::unexpected(ERR(InvalidArgument)),
                    "Invalid PubSub transport: %s", transporterName);
        FAIL_IF(this->hasTransport(transportId),
                std::unexpected(ERR(AlreadyExists)),
                "Transport with ID " SV_FMT " already exists in %s",
                MAGIC_SV_ARG(Spec::Transport, transportId), this->ownerName());
        auto entry = TransporterEntry{
            .transportId = transportId,
            .transporter = transporter,
            .topicMask = static_cast<TopicMask>(Spec::Topic::PubSub),
            .name = transporterName,
        };
        _log_i("%s: add transport " SV_FMT " (%u) with topic mask 0x%02x",
               this->ownerName(), SV_ARG(entry.name), entry.transportId,
               static_cast<unsigned>(entry.topicMask));
        return _addImpl(transportId, entry);
    }

    ReturnCode subscribeTransport(TransportId transportId,
                                  TopicMask topicMask) {
        _log_i("%s: subscribe transport %u to topic mask 0x%02x",
               this->ownerName(), transportId,
               static_cast<unsigned>(topicMask));
        return _withEntryId(transportId, [&topicMask](const EntryKey &,
                                                      TransporterEntry &entry) {
            entry.topicMask |= topicMask;
            return OK();
        });
    }

    ReturnCode unsubscribeTransport(TransportId transportId,
                                    TopicMask topicMask) {
        _log_i("%s: unsubscribe transport %u from topic mask 0x%02x",
               this->ownerName(), transportId,
               static_cast<unsigned>(topicMask));
        return _withEntryId(transportId, [&topicMask](const EntryKey &,
                                                      TransporterEntry &entry) {
            entry.topicMask &= ~topicMask;
            return OK();
        });
    }

    [[nodiscard]] bool hasTransport(TransportId transportId) const {
        FAIL_IF(transportId == 0, false, "Invalid transport ID: 0");
        return any(
            [transportId](const EntryKey &, const TransporterEntry &entry) {
                return entry.transportId == transportId;
            });
    }

    template <typename Filter>
        requires(std::is_invocable_r_v<bool, Filter, const EntryKey &,
                                       const TransporterEntry &>)
    [[nodiscard]] std::expected<Snapshot, ReturnCode>
    snapshot(Filter &&filter) const {
        Snapshot out{};
        FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
            keys, this->snapshotKeys(std::forward<Filter>(filter)),
            "Failed to snapshot transport keys for %s", this->ownerName());

        for (size_t i = 0; i < keys.count; ++i) {
            FAIL_IF_ERR_FWD_UNEXPECTED(
                this->withEntryConst(keys.keys[i],
                                     [&](const TransporterEntry &entry) {
                                         out.entries[out.count++] = entry;
                                         return OK();
                                     }),
                "Failed to snapshot transport entry %u for %s",
                static_cast<unsigned>(keys.keys[i]), this->ownerName());
        }

        return out;
    }

    [[nodiscard]] std::expected<Snapshot, ReturnCode> snapshot() const {
        return snapshot(
            [](const EntryKey &, const TransporterEntry &) { return true; });
    }

  private:
    template <typename Fn>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const EntryKey &,
                                       TransporterEntry &>)
    ReturnCode _withEntryId(TransportId transportId, Fn &&fn) {
        FAIL_IF(transportId == 0, ERR(InvalidArgument),
                "Invalid transport ID: 0");
        auto fnRef = std::ref(fn);
        auto filter = [&transportId](const EntryKey &,
                                     const TransporterEntry &entry) {
            return entry.transportId == transportId;
        };
        return this->withAll(fnRef, std::move(filter));
    }
};

} // namespace Totem::PubSubBackend::detail
