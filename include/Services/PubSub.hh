#pragma once

#include "Data.hh"
#include "Macros/Facade.hh"
#include "PubSubBackend/Facade.hh" // IWYU pragma: export
#include "Types/Error.hh"
#include <cstring>

class PubSubService {
    using Node = Totem::PubSubBackend::Node;

    inline static Node *backend = nullptr;

  public:
    using Topic = NodeData::PubSub::Topic;

    static void setBackend(Node &backendNode) { backend = &backendNode; }

    static Node &node() {
        ABORT_IF_NULL(backend, "PubSub backend node is not set");
        return *backend;
    }

    // static ReturnCode publish(const FrameView &frameView) {
    //     return node().publish(frameView);
    // }
    //
    // static std::expected<Frame, ReturnCode>
    // makeFrame(Topic topic, std::span<const std::byte> bytes) {
    //     return Frame::make(node().nodeId(), topic, bytes);
    // }

  private:
    using DefaultError = CoreError;
};
