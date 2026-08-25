#pragma once

#include "Concepts/Performance.hpp"
#include <array>
#include <bit>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace Totem::Generic {

/**
 * Owning, allocation-free callback for ISR-facing component boundaries.
 *
 * The callback object is copied into fixed inline storage. Requiring a small,
 * trivial capture keeps invocation deterministic and makes it safe to copy the
 * callback between ordinary and interrupt-driven components.
 */
template <typename Argument, size_t MaxCaptureSize = 32> class InlineCallback {
    using Storage = std::array<std::byte, MaxCaptureSize>;
    using Invoke = void (*)(const Storage &storage, Argument argument);

  public:
    template <typename Callback>
    explicit InlineCallback(Callback callback)
        : _invoke(&_invokeStored<std::decay_t<Callback>>) {
        using StoredCallback = std::decay_t<Callback>;
        static_assert(IsTinyTrivialValue<StoredCallback, MaxCaptureSize>,
                      "Inline callback capture must be small and trivially "
                      "copyable");
        static_assert(std::invocable<StoredCallback &, Argument>,
                      "Inline callback cannot accept the required argument");
        _store(std::move(callback));
    }

    void operator()(Argument argument) const { _invoke(_storage, argument); }

  private:
    template <typename Callback> void _store(const Callback &callback) {
        const auto bytes =
            std::bit_cast<std::array<std::byte, sizeof(Callback)>>(callback);
        for (size_t i = 0; i < bytes.size(); ++i) {
            _storage[i] = bytes[i];
        }
    }

    template <typename Callback>
    [[nodiscard]] static Callback _load(const Storage &storage) {
        std::array<std::byte, sizeof(Callback)> bytes{};
        for (size_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = storage[i];
        }
        return std::bit_cast<Callback>(bytes);
    }

    template <typename Callback>
    static void _invokeStored(const Storage &storage, Argument argument) {
        auto callback = _load<Callback>(storage);
        (void)std::invoke(callback, argument);
    }

    Storage _storage{};
    Invoke _invoke;
};

} // namespace Totem::Generic
