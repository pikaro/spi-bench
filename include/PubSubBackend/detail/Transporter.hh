#pragma once

#include "PubSubBackend/detail/Types.hh"
#include "Types/Error.hh"
#include <concepts>
#include <string_view>

namespace Totem::PubSubBackend::detail {

struct Transporter {
    template <class T> struct Contract {
        static_assert(
            requires(T &cls) {
                { cls.work() } -> std::same_as<ReturnCode>;
            }, "T must provide work");
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

    ReturnCode (*workHook)(void *) = nullptr;
    ReturnCode (*enqueueHook)(void *, FrameHandle frameHandle) = nullptr;
    ReturnCode (*pollIntoHook)(void *, void *ctx,
                               PollIntoCallback callback) = nullptr;
    TransportId (*transportIdHook)(void *) = nullptr;
    std::string_view (*instanceNameHook)(void *) = nullptr;

    ReturnCode work() const { return workHook(self); }
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
            .workHook = [](void *ptr) -> ReturnCode {
                return static_cast<T *>(ptr)->work();
            },
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
            .pollIntoHook = [](void *ptr, void *ctx,
                               PollIntoCallback callback) -> ReturnCode {
                return static_cast<T *>(ptr)->pollInto(ctx, callback);
            },
            .enqueueHook = [](void *ptr,
                              FrameHandle frameHandle) -> ReturnCode {
                return static_cast<T *>(ptr)->enqueue(frameHandle);
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
};

} // namespace Totem::PubSubBackend::detail
