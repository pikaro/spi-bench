#pragma once

#include "Common.hh"

#include "Generic/Directory.hh"
#include "PubSubBackend/detail/Transporter.hh"
#include "PubSubBackend/detail/Types.hh"
#include "Types/Error.hh"
#include "magic_enum/magic_enum.hpp"
#include <cstring>
#include <expected>

namespace Totem::PubSubBackend::detail {

struct TransporterEntry {
    TransportId transportId{};
    Transporter transporter{};
    TopicMask topicMask = 0;
};

using TransporterDirectoryImpl =
    Generic::Directory<TransporterEntry, Spec::Limits::maxTransports,
                       Spec::Limits::maxTransportNameLength>;

class TransporterDirectory : public TransporterDirectoryImpl {
    using Base = TransporterDirectoryImpl;

  public:
    explicit TransporterDirectory(const char *ownerName) : Base(ownerName) {}

    using EntryNameKey = typename Base::EntryNameKey;

    std::expected<EntryNameKey, ReturnCode> add(TransportId transportId,
                                                const char *transporterName,
                                                Transporter transporter) {
        FAIL_IF_NULL(transporterName, std::unexpected(ERR(InvalidArgument)),
                     "%s: Runner name cannot be null", this->ownerName());
        auto nameKey = EntryNameKey::fromCharPtr(transporterName);
        return add(transportId, nameKey, transporter);
    }

    std::expected<EntryNameKey, ReturnCode>
    add(TransportId transportId, const EntryNameKey &transporterNameKey,
        Transporter transporter) {
        FAIL_IF_NOT(
            transporter.validate(), std::unexpected(ERR(InvalidArgument)),
            "Invalid PubSub transport: %s", transporterNameKey.name.data());
        FAIL_IF(this->hasTransport(transportId),
                std::unexpected(ERR(AlreadyExists)),
                "Transport with ID " SV_FMT " already exists in %s",
                SV_ARG(magic_enum::enum_name(
                    static_cast<Spec::Transport>(transportId))),
                this->ownerName());
        auto entry = TransporterEntry{
            .transportId = transportId,
            .transporter = transporter,
            .topicMask = 0,
        };
        return _addImpl(transporterNameKey, entry);
    }

    ReturnCode subscribeTransport(TransportId transportId,
                                  TopicMask topicMask) {
        return _withEntryId(transportId,
                            [&topicMask](const EntryNameKey & /*nameKey*/,
                                         TransporterEntry &entry) {
                                entry.topicMask |= topicMask;
                                return OK();
                            });
    }

    ReturnCode unsubscribeTransport(TransportId transportId,
                                    TopicMask topicMask) {
        return _withEntryId(transportId,
                            [&topicMask](const EntryNameKey & /*nameKey*/,
                                         TransporterEntry &entry) {
                                entry.topicMask &= ~topicMask;
                                return OK();
                            });
    }

    [[nodiscard]] bool hasTransport(TransportId transportId) const {
        return any([transportId](const EntryNameKey & /*nameKey*/,
                                 const TransporterEntry &entry) {
            return entry.transportId == transportId;
        });
    }

  private:
    template <typename Fn>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const EntryNameKey &,
                                       TransporterEntry &>)
    ReturnCode _withEntryId(TransportId transportId, Fn &&fn) {
        return this->withAll(std::forward<Fn>(fn),
                             [&transportId](const EntryNameKey & /*nameKey*/,
                                            TransporterEntry &entry) {
                                 return entry.transportId == transportId;
                             });
    }

    using DefaultError = CoreError;
};

} // namespace Totem::PubSubBackend::detail
