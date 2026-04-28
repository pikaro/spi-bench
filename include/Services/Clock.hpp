#pragma once

#include <cstdlib>
#include <cstdint>
#include <optional>

namespace Totem::Clock::detail {

struct IClock {
    virtual ~IClock() = default;

    [[nodiscard]] virtual int64_t nowUs() const = 0;
    [[nodiscard]] virtual uint32_t nowMs() const = 0;
    [[nodiscard]] virtual bool synced() const = 0;
    [[nodiscard]] virtual std::optional<int64_t> drift() const = 0;
};

class NullClock : public IClock {
  public:
    [[nodiscard]] int64_t nowUs() const override { return 0; }
    [[nodiscard]] uint32_t nowMs() const override { return 0; }
    [[nodiscard]] bool synced() const override { return false; }
    [[nodiscard]] std::optional<int64_t> drift() const override {
        return std::nullopt;
    }
};

static inline NullClock nullClock{};

} // namespace Totem::Clock::detail

class ClockService {
    using IClock = Totem::Clock::detail::IClock;

    inline static IClock *_clock = &Totem::Clock::detail::nullClock;

  public:
    static void set(IClock &clock) { _clock = &clock; }

    static IClock &get() {
        if (_clock == nullptr) {
            std::abort();
        }
        return *_clock;
    }
};
