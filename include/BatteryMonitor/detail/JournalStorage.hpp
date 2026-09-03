// IWYU pragma: private

#pragma once

#include "BatteryMonitor/detail/JournalFormat.hpp"
#include "Types/Error.hpp"
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace Totem::BatteryMonitor::detail {

[[nodiscard]] constexpr bool journalRecordsFit(uint64_t currentRecords,
                                               uint64_t additionalRecords,
                                               uint64_t maximumRecords,
                                               uint64_t freeBytes) {
    if (currentRecords > maximumRecords ||
        additionalRecords > maximumRecords - currentRecords) {
        return false;
    }
    return additionalRecords <=
           freeBytes / static_cast<uint64_t>(journalRecordSize);
}

template <typename Sink>
concept JournalAppendSink =
    requires(Sink sink, std::span<const std::byte> bytes) {
        { sink.open() } -> std::same_as<ReturnCode>;
        {
            sink.write(bytes)
        } -> std::same_as<std::expected<std::size_t, ReturnCode>>;
        { sink.flush() } -> std::same_as<ReturnCode>;
        { sink.close() } -> std::same_as<ReturnCode>;
    };

/** Executes one open/write/flush/close attempt without an internal retry. */
template <JournalAppendSink Sink>
[[nodiscard]] ReturnCode appendJournalRecord(Sink &sink,
                                             const JournalRecord &record) {
    auto ret = sink.open();
    if (!ret.ok()) {
        return ret;
    }
    auto written = sink.write(std::span<const std::byte>{record});
    if (!written) {
        (void)sink.close();
        return written.error();
    }
    if (*written != record.size()) {
        (void)sink.close();
        return ReturnCode::from(CoreError::InvalidSize);
    }
    ret = sink.flush();
    if (!ret.ok()) {
        (void)sink.close();
        return ret;
    }
    return sink.close();
}

} // namespace Totem::BatteryMonitor::detail
