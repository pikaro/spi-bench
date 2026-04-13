#pragma once

#include "Common.hh"

#include "Generic/Directory.hh"
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
    TopicMask topicMask = 0;
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
                                            const char *transporterName,
                                            Transporter transporter) {
        FAIL_IF_NULL(transporterName, std::unexpected(ERR(InvalidArgument)),
                     "%s: Runner name cannot be null", this->ownerName());
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
        return _addImpl(transportId, entry);
    }

    std::expected<EntryKey, ReturnCode> add(TransportId transportId,
                                            std::string_view transporterName,
                                            Transporter transporter) {
        FAIL_IF(transporterName.empty(), std::unexpected(ERR(InvalidArgument)),
                "%s: Transporter name cannot be empty", this->ownerName());
        return add(transportId, transporterName.data(), transporter);
    }

    ReturnCode subscribeTransport(TransportId transportId,
                                  TopicMask topicMask) {
        return _withEntryId(transportId, [&topicMask](const EntryKey &,
                                                      TransporterEntry &entry) {
            entry.topicMask |= topicMask;
            return OK();
        });
    }

    ReturnCode unsubscribeTransport(TransportId transportId,
                                    TopicMask topicMask) {
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

    using DefaultError = CoreError;
};

} // namespace Totem::PubSubBackend::detail
