#pragma once

#include "Generic/Directory.hh"
#include "Macros/Facade.hh"
#include "PubSubBackend/detail/Transporter.hh"
#include "PubSubBackend/detail/Types.hh"
#include "Types/Error.hh"
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
    explicit TransporterDirectory(const char *ownerName) : Base(ownerName) {}

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
            .topicMask = 0,
            .name = transporterName,
        };
        _log_i("%s: add transport " SV_FMT " (%u)", this->ownerName(),
               SV_ARG(entry.name), entry.transportId);
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
