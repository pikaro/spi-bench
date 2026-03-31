#pragma once

#include "Common.hh"

#include "Concepts/Base.hh"
#include "Types/Logging.hh"

namespace Totem::Output::detail {

struct Sink {
    template <class T> struct Contract {
        static_assert(
            requires(T &cls, const LogRecord &record) {
                { cls.write(record) } -> std::same_as<ReturnCode>;
            }, "T must provide write");
        static_assert(IsNamedEntity<T>, "T must be a named entity");
    };

    // Virtual dispatch for swappable sinks
    void *self = nullptr;

    const char *name = "UnnamedSink";
    bool active = false;

    ReturnCode (*writeHook)(void *, const LogRecord &record) = nullptr;

    ReturnCode write(const LogRecord &record) const {
        return active ? writeHook(self, record) : OK(CoreError);
    }

    template <class T>
        requires requires { sizeof(Contract<T>); }
    static Sink bind(T &obj, bool active = true) {
        return Sink{
            .self = std::addressof(obj),
            .name = obj.name,
            .active = active,
            .writeHook = [](void *ptr, const LogRecord &data) -> ReturnCode {
                return static_cast<T *>(ptr)->write(data);
            },
        };
    }

    [[nodiscard]] bool validate() const {
        return self != nullptr && writeHook != nullptr;
    }
};

} // namespace Totem::Output::detail
