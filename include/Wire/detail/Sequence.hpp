#pragma once

#include <atomic>
#include <cstdint>
namespace Totem::Wire::detail {

template <typename T = uint8_t>
struct Sequence {
    T next() {
        return _sequenceNumber.fetch_add(1, std::memory_order_relaxed);
    }

    // bool is(uint8_t num) {
    //     return num == _sequenceNumber.load(std::memory_order_relaxed);
    // }
    //
    // bool nextIs(uint8_t num) {
    //     return num ==
    //            (_sequenceNumber.load(std::memory_order_relaxed) + 1) % 256;
    // }

    bool received(T num) {
        auto expected = num;
        return _sequenceNumber.compare_exchange_strong(
            expected, static_cast<T>(num + 1), std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    [[nodiscard]] T current() const {
        return _sequenceNumber.load(std::memory_order_acquire);
    }

    void reset(T num = 0) {
        _sequenceNumber.store(num, std::memory_order_release);
    }

  private:
    std::atomic<T> _sequenceNumber = 0;
};

} // namespace Totem::Wire::detail
