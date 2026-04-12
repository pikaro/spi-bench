#pragma once

#include "Common.hh"

#include "Generic/Directory.hh"
#include "PubSubBackend/Interfaces/Frame.hh"
#include "PubSubBackend/Interfaces/Subscriber.hh"
#include "PubSubBackend/detail/Types.hh"
#include <cstring>
#include <expected>

namespace Totem::PubSubBackend::detail {

struct SubscriberEntry {
    SubscriberCallback callback;
    TopicId topic;
};

using SubscriberDirectoryImpl =
    Generic::Directory<SubscriberEntry, Spec::Limits::maxSubscribers,
                       Spec::Limits::maxSubscriberNameLength>;

class SubscriberDirectory : public SubscriberDirectoryImpl {
    using Base = SubscriberDirectoryImpl;

  public:
    explicit SubscriberDirectory(const char *ownerName) : Base(ownerName) {}

    using EntryNameKey = typename Base::EntryNameKey;

    std::expected<EntryNameKey, ReturnCode>
    add(const char *subscriberName,
        const SubscriberCallback &subscriberCallback, TopicId topic) {
        FAIL_IF_NULL(subscriberName, std::unexpected(ERR(InvalidArgument)),
                     "%s: PubSub subscriber name cannot be null",
                     this->ownerName());
        auto nameKey = EntryNameKey::fromCharPtr(subscriberName);
        return add(nameKey, subscriberCallback, topic);
    }

    std::expected<EntryNameKey, ReturnCode>
    add(const EntryNameKey &subscriberNameKey,
        const SubscriberCallback &subscriberCallback, TopicId topic) {
        auto entry = SubscriberEntry{
            .callback = subscriberCallback,
            .topic = topic,
        };
        return this->_addImpl(subscriberNameKey, entry);
    }

    [[nodiscard]] std::expected<TopicId, ReturnCode>
    topicForSubscriber(const EntryNameKey &subscriberNameKey) const {
        TopicId topic;
        auto ret = this->withEntryConst(subscriberNameKey,
                                        [&topic](const SubscriberEntry &entry) {
                                            topic = entry.topic;
                                            return OK();
                                        });
        FAIL_IF_ERR(ret, std::unexpected(ret),
                    "Failed to get topic for subscriber %s->%s",
                    this->ownerName(), subscriberNameKey.name.data());
        return topic;
    }

  private:
    using DefaultError = CoreError;
};

} // namespace Totem::PubSubBackend::detail
