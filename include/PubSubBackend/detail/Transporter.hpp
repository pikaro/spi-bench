#pragma once

#include "PubSubBackend/detail/Types.hpp"
#include "Types/Error.hpp"
#include <concepts>
#include <cstddef>
#include <limits>
#include <string_view>

namespace Totem::PubSubBackend::detail {

struct Transporter {
    template <class T> struct Contract {
        static_assert(
            requires(T &cls, size_t maxCount) {
                { cls.send(maxCount) } -> std::same_as<ReturnCode>;
            }, "T must provide send");
        static_assert(
            requires(T &cls, size_t maxCount) {
                { cls.receive(maxCount) } -> std::same_as<ReturnCode>;
            }, "T must provide receive");
        static_assert(
            requires(T &cls, void *ctx, PollIntoCallback callback) {
                { cls.pollInto(ctx, callback) } -> std::same_as<ReturnCode>;
            }, "T must provide pollInto");
        static_assert(
            requires(T &cls, FrameHandle frameHandle) {
                { cls.enqueue(frameHandle) } -> std::same_as<ReturnCode>;
            }, "T must provide enqueue");
        static_assert(
            requires(T &cls) {
                { cls.transportId() } -> std::same_as<TransportId>;
            }, "T must provide transportId");
        static_assert(
            requires(T &cls) {
                { cls.instanceName() } -> std::same_as<std::string_view>;
            }, "T must provide instanceName");
    };

    void *self = nullptr;

    ReturnCode (*sendHook)(void *, size_t) = nullptr;
    ReturnCode (*receiveHook)(void *, size_t) = nullptr;
    ReturnCode (*enqueueHook)(void *, FrameHandle frameHandle) = nullptr;
    ReturnCode (*pollIntoHook)(void *, void *ctx,
                               PollIntoCallback callback) = nullptr;
    TransportId (*transportIdHook)(void *) = nullptr;
    std::string_view (*instanceNameHook)(void *) = nullptr;

    ReturnCode send(size_t maxCount = All) const {
        return sendHook(self, maxCount);
    }
    ReturnCode receive(size_t maxCount = All) const {
        return receiveHook(self, maxCount);
    }
    ReturnCode enqueue(FrameHandle frameHandle) const {
        return enqueueHook(self, frameHandle);
    }
    ReturnCode pollInto(void *ctx, PollIntoCallback callback) const {
        return pollIntoHook(self, ctx, callback);
    }
    [[nodiscard]] TransportId transportId() const {
        return transportIdHook(self);
    }
    [[nodiscard]] std::string_view instanceName() const {
        return instanceNameHook(self);
    }

    template <class T>
        requires requires { sizeof(Contract<T>); }
    static Transporter bind(T &obj) {
        return Transporter{
            .self = std::addressof(obj),
            .sendHook = [](void *ptr, size_t maxCount = All) -> ReturnCode {
                return static_cast<T *>(ptr)->send(maxCount);
            },
            .receiveHook = [](void *ptr, size_t maxCount = All) -> ReturnCode {
                return static_cast<T *>(ptr)->receive(maxCount);
            },
            .enqueueHook = [](void *ptr,
                              FrameHandle frameHandle) -> ReturnCode {
                return static_cast<T *>(ptr)->enqueue(frameHandle);
            },
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
            .pollIntoHook = [](void *ptr, void *ctx,
                               PollIntoCallback callback) -> ReturnCode {
                return static_cast<T *>(ptr)->pollInto(ctx, callback);
            },
            .transportIdHook = [](void *ptr) -> TransportId {
                return static_cast<T *>(ptr)->transportId();
            },
            .instanceNameHook = [](void *ptr) -> std::string_view {
                return static_cast<T *>(ptr)->instanceName();
            },
        };
    }

    [[nodiscard]] bool validate() const {
        return self != nullptr && pollIntoHook != nullptr &&
               enqueueHook != nullptr;
    }

  private:
    static constexpr size_t All = std::numeric_limits<size_t>::max();
};

} // namespace Totem::PubSubBackend::detail
