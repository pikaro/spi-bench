#pragma once

#include <atomic>
#include <cstdint>
namespace Totem::Wire::detail {

struct Sequence {
    uint8_t next() {
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

    bool received(uint8_t num) {
        return _sequenceNumber.compare_exchange_strong(
            num, (num + 1) % 256, std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

  private:
    std::atomic<uint8_t> _sequenceNumber = 0;
};

} // namespace Totem::Wire::detail
