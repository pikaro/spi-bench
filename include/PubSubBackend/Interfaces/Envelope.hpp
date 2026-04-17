#pragma once

#include "Data.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "PubSubBackend/Interfaces/Wire.hpp" // IWYU pragma: export
#include "PubSubBackend/detail/Codec.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>

namespace Totem::PubSubBackend {

struct Envelope;

template <typename T> inline constexpr std::byte PayloadTypeTag{};

using PayloadPtrGetter = std::expected<const void *, ReturnCode> (*)(
    void *owner, const Envelope &envelope);
using PayloadGetter = ReturnCode (*)(void *owner, const Envelope &envelope,
                                     size_t offset, std::span<std::byte> buf);
using ReleaseCallback = ReturnCode (*)(void *owner, const Envelope &envelope);
using NextMessageIdCallback = MessageId (*)(void *owner);
using PayloadEncoder = ReturnCode (*)(void *owner, const Envelope &envelope,
                                      std::span<std::byte> out);

struct EnvelopeDef {
    using Topic = typename NodeData::PubSub::Topic;

    void *owner;
    Topic topic;
    MessageId messageId;
    PayloadPtrGetter getPayloadPtr = nullptr;
    PayloadGetter getPayload = nullptr;
    PayloadEncoder encodePayload = nullptr;
    ReleaseCallback release;

    [[nodiscard]] bool valid() const {
        return static_cast<uint32_t>(topic) != 0 && release != nullptr &&
               ((getPayloadPtr != nullptr && encodePayload != nullptr) ||
                getPayload != nullptr) &&
               messageId != 0;
    }
};

struct Envelope {
    using Topic = typename NodeData::PubSub::Topic;

    Header header{};
    void *owner = nullptr;
    PayloadGetter getPayload = nullptr;
    PayloadPtrGetter getPayloadPtr = nullptr;
    PayloadEncoder encodePayload = nullptr;
    const void *typeTag = nullptr;
    ReleaseCallback release = nullptr;

    ReturnCode ack() const { return release(owner, *this); }

    template <typename T> [[nodiscard]] bool hasTyped() const {
        return getPayloadPtr != nullptr && typeTag != nullptr &&
               &PayloadTypeTag<T> == typeTag;
    }

    [[nodiscard]] bool valid() const {
        return owner != nullptr &&
               (getPayload != nullptr ||
                (getPayloadPtr != nullptr && typeTag != nullptr &&
                 encodePayload != nullptr)) &&
               release != nullptr;
    }

    bool operator==(Envelope other) const { return header == other.header; }

    template <typename T> std::expected<T, ReturnCode> getPayloadAs() const {
        if (hasTyped<T>()) {
            FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(
                ptr, getPayloadPtr(owner, *this),
                "Failed to get typed payload pointer");
            if (ptr == nullptr) {
                return std::unexpected(ERR(InvalidData));
            }
            return *static_cast<const T *>(ptr);
        }

        FAIL_IF_NULL(getPayload, std::unexpected(ERR(InvalidState)),
                     "Envelope does not have a payload getter");

        struct BoundReader {
            const Envelope *envelope;

            static ReturnCode read(void *owner, size_t offset,
                                   std::span<std::byte> out) {
                auto *self = static_cast<BoundReader *>(owner);
                return self->envelope->getPayload(self->envelope->owner,
                                                  *self->envelope, offset, out);
            }
        };

        auto bound = BoundReader{.envelope = this};
        return detail::Codec<T>::decode({
            .read = &BoundReader::read,
            .owner = &bound,
            .size = header.payloadSize,
        });
    }

    template <class T>
        requires detail::is_wire_message_v<T>
    static std::expected<Envelope, ReturnCode> make(const EnvelopeDef &def) {
        constexpr auto payloadSize = detail::Codec<T>::encodedSize();
        static_assert(
            payloadSize <= NodeData::PubSub::Limits::maxPayloadSize,
            "Wire message payload exceeds PubSub::Limits::maxPayloadSize");
        FAIL_IF_NOT(
            def.valid(), std::unexpected(ERR(InvalidArgument)),
            "Invalid EnvelopeDef for topic " SV_FMT,
            SV_ARG(magic_enum::enum_name(static_cast<Topic>(def.topic))));
        return Envelope{
            .header =
                {
                    .timestampMs = ::platform::get_time(),
                    .messageId = def.messageId,
                    .topic = static_cast<TopicId>(def.topic),
                    .source = static_cast<NodeId>(NodeData::PubSub::nodeId),
                    .payloadSize = static_cast<uint16_t>(payloadSize),
                },
            .owner = def.owner,
            .getPayload = def.getPayload,
            .getPayloadPtr = def.getPayloadPtr,
            .encodePayload = def.encodePayload,
            .typeTag = &PayloadTypeTag<T>,
            .release = def.release,
        };
    }
};

} // namespace Totem::PubSubBackend
