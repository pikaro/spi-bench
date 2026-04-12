#pragma once

#include "Macros/Facade.hh"
#include "PubSubBackend/Interfaces/Envelope.hh"
#include "PubSubBackend/detail/ControlPlane.hh"
#include "PubSubBackend/detail/Publisher.hh"
#include "PubSubBackend/detail/TransporterDirectory.hh"
#include "PubSubBackend/detail/Types.hh"
#include "Queue/Facade.hh"
#include "Types/Error.hh"
#include "magic_enum/magic_enum.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>

namespace Totem::PubSubBackend::detail {

struct DrainerDependencies {
    TransporterDirectory &transporters;
    Publisher &publisher;
    ControlPlane &controlPlane;
    Totem::Queue::Handle *publishQueue;

    [[nodiscard]] bool validate() const { return publishQueue != nullptr; }
};

class Drainer {
    using TransporterNameKey = TransporterDirectory::EntryNameKey;

    static constexpr auto kMaxInFlightMessages =
        Spec::Limits::maxInFlightMessages;

  public:
    explicit Drainer(const DrainerDependencies &deps)
        : _transporters(deps.transporters), _publisher(deps.publisher),
          _controlPlane(deps.controlPlane), _publishQueue(deps.publishQueue) {
        ABORT_IF_NOT(deps.validate(), "Invalid Drainer dependencies");
    }

    ReturnCode drain() {
        auto ret = OK();
        ret.combine(_publishFromQueue());
        ret.combine(_publishFromTransports());
        return ret;
    }

    ReturnCode ack(Spec::Transport transportId, const Envelope &req) {
        auto ret = OK();
        auto mask = static_cast<TransportMask>(transportId);
        for (size_t i = 0; i < kMaxInFlightMessages; ++i) {
            auto &frame = _inFlightFrames[i];
            if (frame.valid() && (frame.pendingMask & mask) != 0) {
                if (frame.envelope.header.messageId == req.header.messageId) {
                    frame.pendingMask &= ~mask;
                    if (--frame.pendingCount == 0) {
                        if (frame.envelope.release != nullptr) {
                            ret.combine(frame.envelope.release(
                                frame.envelope.owner, req));
                        }
                        frame = StoredFrame{};
                    }
                    return ret;
                }
            }
        }
        FAIL(ERR(NotFound),
             "No in-flight message found for ack with messageId "
             "%u from transport " SV_FMT,
             req.header.messageId, SV_ARG(magic_enum::enum_name(transportId)));
    }

  private:
    ReturnCode _publishFromQueue() {
        Envelope item;
        auto ret = OK();
        while (ret.ok()) {
            ret.combine(
                Totem::Queue::Platform::receive(*_publishQueue, &item, 0));
            if (ret.ok()) {
                ret.combine(_publishFrame(item));
            }
        }
        if (ret == ERR(Timeout)) {
            return OK();
        }
        FAIL(ret, "Failed to receive publish request from queue: " ERR_FMT,
             ERR_ARG(ret));
    }

    struct PublishContext {
        Drainer *self;
        TransportId ingressTransport;
    };

    static ReturnCode _publishCallback(void *ctx, const Envelope &req) {
        auto *publishContext = static_cast<PublishContext *>(ctx);
        return publishContext->self->_publishFrame(
            req, publishContext->ingressTransport);
    }

    ReturnCode _publishFromTransports() {
        return _transporters.withAllConst(
            [&](const TransporterNameKey & /*unused*/,
                const TransporterEntry &entry) -> ReturnCode {
                auto ctx = PublishContext{
                    .self = this, .ingressTransport = entry.transportId};
                return entry.transporter.pollInto(&ctx, &_publishCallback);
            });
    }

    std::expected<FrameHandle, ReturnCode> _storeFrame(const Envelope &req) {
        StoredFrame frame;
        frame.envelope = req;
        TransportMask mask = 0;
        uint8_t pendingCount = 0;
        (void)_transporters.withAll(
            [&](const TransporterNameKey & /*nameKey*/,
                const TransporterEntry &entry) -> ReturnCode {
                if ((entry.topicMask & req.header.topic) != 0) {
                    mask |= entry.transportId;
                    ++pendingCount;
                }
                return OK();
            });
        if (pendingCount == 0) {
            return std::unexpected(ERR(NotFound));
        }
        frame.pendingMask = mask;
        frame.pendingCount = pendingCount;
        int16_t stored = -1;
        for (size_t i = 0; i < kMaxInFlightMessages; ++i) {
            if (_inFlightFrames[i].pendingCount == 0) {
                _inFlightFrames[i] = frame;
                stored = static_cast<int16_t>(i);
                break;
            }
        }
        FAIL_IF(stored < 0, std::unexpected(ERR(Overflow)),
                "No space to store in-flight message");
        return &_inFlightFrames[static_cast<size_t>(stored)];
    }

    ReturnCode
    _publishFrame(const Envelope &item,
                  std::optional<TransportId> ingressTransport = std::nullopt) {
        auto ret = OK();
        ret.combine(_controlPlane.handle(item, ingressTransport));
        ret.combine(_publisher.publishToSubscribers(item));
        auto storeResult = _storeFrame(item);
        if (!storeResult) {
            if (storeResult.error() == ERR(NotFound)) {
                FAIL_IF_ERR_FWD(item.release(item.owner, item),
                                "Failed to release message for topic " SV_FMT
                                " with no subscribers or transports",
                                MAGIC_SV_ARG(Spec::Topic, item.header.topic));
                return ret;
            }
            FAIL(storeResult.error(),
                 "Failed to store in-flight message: " ERR_FMT,
                 ERR_ARG(storeResult.error()));
        }
        ret.combine(
            _publisher.publishToTransports(*storeResult, ingressTransport));
        return ret;
    }

    TransporterDirectory &_transporters;
    Publisher &_publisher;
    ControlPlane &_controlPlane;
    Totem::Queue::Handle *_publishQueue;
    std::array<StoredFrame, kMaxInFlightMessages> _inFlightFrames{};

    using DefaultError = CoreError;
};

} // namespace Totem::PubSubBackend::detail
