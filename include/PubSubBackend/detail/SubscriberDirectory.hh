#pragma once

#include "Common.hh"

#include "Generic/Directory.hh"
#include "PubSubBackend/Interfaces/Subscriber.hh"
#include "PubSubBackend/Interfaces/Types.hh"
#include "PubSubBackend/detail/Types.hh"
#include <cstdint>
#include <cstring>
#include <expected>
#include <string_view>

namespace Totem::PubSubBackend::detail {

struct SubscriberEntry {
    void *subscriber;
    SubscriberCallback callback;
    TopicId topic;
    std::string_view name;
};

using SubscriberDirectoryImpl =
    Directory<uintptr_t, SubscriberEntry, Spec::Limits::maxSubscribers>;

class SubscriberDirectory : public SubscriberDirectoryImpl {
    using Base = SubscriberDirectoryImpl;

  public:
    explicit SubscriberDirectory(const char *ownerName) : Base(ownerName) {}

    using EntryKey = typename Base::EntryKey;

    std::expected<EntryKey, ReturnCode> add(const char *subscriberName,
                                            const Subscriber &subscriber,
                                            TopicId topic) {
        FAIL_IF_NULL(subscriberName, std::unexpected(ERR(InvalidArgument)),
                     "%s: PubSub subscriber name cannot be null",
                     this->ownerName());
        auto entry = SubscriberEntry{
            .subscriber = subscriber.subscriber,
            .callback = subscriber.callback,
            .topic = topic,
            .name = subscriberName,
        };
        return this->_addImpl(
            reinterpret_cast<uintptr_t>(subscriber.subscriber), entry);
    }

    [[nodiscard]] std::expected<TopicId, ReturnCode>
    topicForSubscriber(const EntryKey &subscriberKey) const {
        TopicId topic;
        auto ret = this->withEntryConst(subscriberKey,
                                        [&topic](const SubscriberEntry &entry) {
                                            topic = entry.topic;
                                            return OK();
                                        });
        FAIL_IF_ERR(ret, std::unexpected(ret),
                    "Failed to get topic for subscriber in %s",
                    this->ownerName());
        return topic;
    }

  private:
    using DefaultError = CoreError;
};

} // namespace Totem::PubSubBackend::detail
