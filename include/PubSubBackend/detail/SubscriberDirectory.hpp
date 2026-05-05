#pragma once

#include "Generic/Directory.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Subscriber.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Types/Error.hpp"
#include <atomic>
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

class SubscriberDirectory;

using SubscriberDirectoryImpl =
    BaseDirectory<SubscriberDirectory, SubscriberKey, SubscriberEntry,
                  Spec::Limits::maxSubscribers>;

class SubscriberDirectory : public SubscriberDirectoryImpl {
  public:
    static constexpr LogComponent logComponent =
        Totem::PubSubBackend::detail::logComponent;

    explicit SubscriberDirectory(const char *ownerName)
        : SubscriberDirectoryImpl(ownerName) {}

    std::expected<SubscriberKey, ReturnCode> add(const char *subscriberName,
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
        return this->_addImpl(nextSubscriberKey(), entry);
    }

    [[nodiscard]] std::expected<TopicId, ReturnCode>
    topicForSubscriber(const SubscriberKey &subscriberKey) const {
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
    [[nodiscard]] SubscriberKey nextSubscriberKey() {
        auto key = _nextKey.fetch_add(1, std::memory_order_relaxed);
        return key == 0 ? nextSubscriberKey() : key;
    }

    std::atomic<SubscriberKey> _nextKey{1};
};

} // namespace Totem::PubSubBackend::detail
