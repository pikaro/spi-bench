#pragma once

#include "Concepts/Base.hh"
#include "Types/Command.hh"
#include "Types/Error.hh"
#include <concepts>
#include <expected>

namespace Totem::CommandBackend::detail {

struct Transport {
    using PollReturn = std::expected<CommandDesc::Tokens, ReturnCode>;

    template <class T> struct Contract {
        static_assert(IsNamedEntity<T>, "T must be a named entity");
        static_assert(requires(T &cls) {
            { cls.poll() } -> std::same_as<PollReturn>;
        });
    };

    void *self = nullptr;

    PollReturn (*pollHook)(void *) = nullptr;

    [[nodiscard]] std::expected<CommandDesc::Tokens, ReturnCode> poll() const {
        return pollHook(self);
    }

    template <class T>
        requires requires { sizeof(Contract<T>); }
    static Transport bind(T &obj) {
        return Transport{
            .self = std::addressof(obj),
            .pollHook = [](void *ptr) -> PollReturn {
                return static_cast<T *>(ptr)->poll();
            },
        };
    }

    [[nodiscard]] bool validate() const {
        return self != nullptr && pollHook != nullptr;
    }
};

} // namespace Totem::CommandBackend::detail
