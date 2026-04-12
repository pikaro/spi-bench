#pragma once

#include "Types/Error.hh"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>

namespace Totem::PubSubBackend {

using MessageId = std::uint32_t;
using NodeId = std::uint8_t;
using TopicId = std::uint32_t;

struct PublishRequest;

using PayloadGetter = std::expected<std::span<const std::byte>, ReturnCode> (*)(
    void *owner, const PublishRequest &req);
using ReleaseCallback = ReturnCode (*)(void *owner, const PublishRequest &req);

struct PublishRequest {
    MessageId messageId{};
    TopicId topic{};
    NodeId source{};
    void *owner = nullptr;
    PayloadGetter getPayload = nullptr;
    ReleaseCallback release = nullptr;
};

// struct Frame {
//     FrameHeader header{};
//     Payload payload{};
//
//     [[nodiscard]] constexpr FrameView view() const noexcept {
//         return FrameView{.topic = topic,
//                          .source = source,
//                          .payload =
//                              std::span<const std::byte>{payload.data(),
//                              size}};
//     }
//
//     [[nodiscard]] static std::expected<Frame, ReturnCode>
//     make(NodeId source, Topic topic, std::span<const std::byte> bytes) {
//         if (bytes.data() == nullptr && !bytes.empty()) {
//             return std::unexpected(ERR(InvalidArgument));
//         }
//
//         if (bytes.size() > PubSubConfig::maxPayloadSize) {
//             return std::unexpected(ERR(Overflow));
//         }
//
//         Frame frame{};
//         frame.topic = topic;
//         frame.source = source;
//         frame.size = static_cast<std::uint16_t>(bytes.size());
//
//         std::memcpy(frame.payload.data(), bytes.data(), bytes.size());
//         return frame;
//     }
//
//   private:
//     using DefaultError = CoreError;
// };

} // namespace Totem::PubSubBackend
