#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "LoggingBackend/Interfaces/Config.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include "LoggingBackend/detail/HasLogLevel.hpp"
#include "LoggingBackend/detail/IRecordSink.hpp"
#include "LoggingBackend/detail/JournalMetrics.hpp"
#include "LoggingBackend/detail/Types.hpp"
#include "Macros/Facade.hpp"
#include "Macros/internal/Markers.hpp"
#include "Mutex/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Services/FileSystem.hpp"
#include "StaticConfig/Logging.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "TaskController/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include "Types/Signal.hpp"
#include <array>
#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>

namespace Totem::LoggingBackend::detail::Outputs {

class ErrorJournalSink
    : public HasLifecycle<ErrorJournalSink, ErrorJournalConfig>,
      public HasTaskController<ErrorJournalSink, ErrorJournalConfig>,
      public HasLogLevel,
      public IRecordSink {
    friend class HasLifecycle<ErrorJournalSink, ErrorJournalConfig>;
    friend struct LifecycleContract<ErrorJournalSink, ErrorJournalConfig>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<ErrorJournalSink, ErrorJournalConfig>;
    friend struct TaskController::TaskHooks::Contract<ErrorJournalSink>;
    friend struct TaskControllerContract<ErrorJournalSink>;

  public:
    explicit ErrorJournalSink(TaskController::IRegistry &registry)
        : HasTaskController<ErrorJournalSink, ErrorJournalConfig>(registry),
          HasLogLevel(name) {}

    DELETE_COPY(ErrorJournalSink)
    DELETE_MOVE(ErrorJournalSink)

    static constexpr const char *name = "Logging::ErrorJournal";
    static constexpr LogComponent logComponent =
        Totem::LoggingBackend::detail::logComponent;

    [[nodiscard]] std::string_view displayName() const override { return name; }

    [[nodiscard]] bool loggingFor(
        LogLevel level,
        std::optional<LogComponent> component = std::nullopt) const override {
        if constexpr (!LoggingConfig::errorJournalEnabled) {
            return false;
        }
        if (!_active.load(std::memory_order_acquire) ||
            _disabled.load(std::memory_order_acquire)) {
            return false;
        }
        if (component && *component == logComponent) {
            return false;
        }
        return HasLogLevel::loggingFor(level, component);
    }

    ReturnCode
    setLogLevel(LogLevel level,
                std::optional<LogComponent> component = std::nullopt) override {
        return HasLogLevel::setLogLevel(level, component);
    }

    ReturnCode setComponentLogLevelDefault(LogComponent component) override {
        return HasLogLevel::setComponentLogLevelDefault(component);
    }

    ReturnCode write(const LogRecord &record) override {
        if constexpr (!LoggingConfig::errorJournalEnabled) {
            return OK();
        }
        if (!_active.load(std::memory_order_acquire) ||
            _disabled.load(std::memory_order_acquire)) {
            return OK();
        }

        bool wakeWriter = false;
        {
            Mutex::ScopedSpinlockGuard guard{_lock};
            if (!_active.load(std::memory_order_relaxed) ||
                _disabled.load(std::memory_order_relaxed)) {
                return OK();
            }

            _appendToHotSlots(record, wakeWriter);
            _captureFirstError(record, wakeWriter);
            _appendHistory(record);
        }

        if (wakeWriter) {
            _wakeWriter();
        }
        return OK();
    }

  private:
    enum class SlotState : uint8_t {
        Free,
        Hot,
    };

    enum class LineKind : uint8_t {
        Header,
        Record,
        Separator,
    };

    enum class RecordTag : uint8_t {
        Pre,
        Error,
        Next,
    };

    struct JournalRecord {
        uint32_t ts = 0;
        uint32_t tsSynced = 0;
        LogSiteId siteId = logUnknownSiteId;
        LogComponent component = LogComponent::Unknown;
        LogLevel level = LogLevel::Off;
        std::array<char, ErrorJournalConfig::journalMessageLength> msg{};

        static JournalRecord from(const LogRecord &record) {
            JournalRecord journalRecord{
                .ts = record.ts,
                .tsSynced = record.tsSynced,
                .siteId = record.siteId,
                .component = record.component,
                .level = record.level,
                .msg = {},
            };

            std::size_t i = 0;
            for (; i + 1 < journalRecord.msg.size() &&
                   i < record.msg.size() && record.msg[i] != '\0';
                 ++i) {
                journalRecord.msg[i] = record.msg[i];
            }
            journalRecord.msg[i] = '\0';
            return journalRecord;
        }
    };

    struct HeaderLine {
        LogSiteId siteId = logUnknownSiteId;
        uint32_t errorTs = 0;
        uint32_t errorTsSynced = 0;
        uint8_t historyCount = 0;
        uint8_t trailingTarget = 0;
    };

    struct RecordLine {
        RecordTag tag = RecordTag::Pre;
        JournalRecord record{};
    };

    struct JournalLine {
        LineKind kind = LineKind::Separator;
        HeaderLine header{};
        RecordLine record{};

        static JournalLine makeHeader(const HeaderLine &line) {
            JournalLine item{};
            item.kind = LineKind::Header;
            item.header = line;
            return item;
        }

        static JournalLine makeRecord(RecordTag tag,
                                      const JournalRecord &record) {
            JournalLine item{};
            item.kind = LineKind::Record;
            item.record = RecordLine{.tag = tag, .record = record};
            return item;
        }

        static JournalLine makeSeparator() {
            JournalLine item{};
            item.kind = LineKind::Separator;
            return item;
        }
    };

    struct ErrorSlot {
        SlotState state = SlotState::Free;
        LogSiteId siteId = logUnknownSiteId;
        uint32_t openedAtMs = 0;
        uint8_t trailingCount = 0;
    };

    struct MetricSnapshot {
        uint32_t caught = 0;
        uint32_t written = 0;
        uint32_t slotDrops = 0;
        uint32_t siteDrops = 0;
        uint32_t queueDrops = 0;
        uint32_t writeFailures = 0;
        uint32_t flashFull = 0;
        uint32_t activeSlots = 0;
        bool disabled = false;
    };

    static_assert(ErrorJournalConfig::historyRecords > 0);
    static_assert(ErrorJournalConfig::trailingRecords > 0);
    static_assert(ErrorJournalConfig::errorSlots > 0);
    static_assert(ErrorJournalConfig::seenSites > 0);
    static_assert(ErrorJournalConfig::queueRecords > 0);
    static_assert(ErrorJournalConfig::journalMessageLength > 0);
    static_assert(ErrorJournalConfig::lineBufferSize >= 32);
    static_assert(ErrorJournalConfig::historyRecords <= UINT8_MAX);
    static_assert(ErrorJournalConfig::trailingRecords <= UINT8_MAX);
    static_assert(ErrorJournalConfig::queueRecords <= UINT8_MAX);

    using File = FileSystemService::DefaultFileSystem::File;

    ReturnCode _onBegin() {
        bool flashFull = false;
        if (!_storageWritable(&flashFull)) {
            _disableQuietly(flashFull);
            _publishMetrics();
            return OK();
        }

        auto hooks = TaskController::TaskHooks::bind(this->derived());
        auto beginRet = this->_beginTaskController();
        if (!beginRet.ok()) {
            _disableQuietly(false);
            _publishMetrics();
            return OK();
        }

        auto taskResult =
            this->_taskController.addTask(this->config().task.name, hooks);
        if (!taskResult) {
            _disableQuietly(false);
            (void)this->_endTaskController();
            _publishMetrics();
            return OK();
        }

        auto startRet =
            this->_taskController.startTask(*taskResult, this->config().task);
        if (!startRet.ok()) {
            _disableQuietly(false);
            (void)this->_endTaskController();
            _publishMetrics();
            return OK();
        }

        _task = *taskResult;
        _active.store(true, std::memory_order_release);
        _publishMetrics();
        return OK();
    }

    ReturnCode _onEnd() {
        _active.store(false, std::memory_order_release);
        if (_task == 0) {
            return OK();
        }
        _task = 0;
        return _endTaskController();
    }

    ReturnCode _onTaskStep() {
        if (!_disabled.load(std::memory_order_acquire)) {
            _closeExpiredSlots();
            _drainQueue();
        }
        _publishMetrics();
        return OK();
    }

    static ReturnCode _onTaskNotify(Signal /*unused*/) { return OK(); }

    void _appendToHotSlots(const LogRecord &record, bool &wakeWriter) {
        JournalRecord journalRecord{};
        bool journalRecordSet = false;
        for (auto &slot : _slots) {
            if (slot.state != SlotState::Hot) {
                continue;
            }

            if (!journalRecordSet) {
                journalRecord = JournalRecord::from(record);
                journalRecordSet = true;
            }
            _enqueueRecord(RecordTag::Next, journalRecord, wakeWriter);
            ++slot.trailingCount;

            if (slot.trailingCount >= ErrorJournalConfig::trailingRecords) {
                _enqueueSeparator(wakeWriter);
                slot = ErrorSlot{};
            }
        }
    }

    void _captureFirstError(const LogRecord &record, bool &wakeWriter) {
        if (record.level != LogLevel::Error ||
            record.siteId == logUnknownSiteId || _siteSeen(record.siteId)) {
            return;
        }

        auto *slot = _findFreeSlot();
        if (slot == nullptr) {
            ++_slotDrops;
            wakeWriter = true;
            return;
        }

        const auto neededLines = 1U + _historyCount + 1U;
        if (_queueAvailable() < neededLines) {
            _queueDrops += static_cast<uint32_t>(neededLines);
            wakeWriter = true;
            return;
        }

        if (!_rememberSite(record.siteId)) {
            ++_siteDrops;
            wakeWriter = true;
            return;
        }

        const auto journalRecord = JournalRecord::from(record);
        _enqueueHeader(
            HeaderLine{
                .siteId = record.siteId,
                .errorTs = record.ts,
                .errorTsSynced = record.tsSynced,
                .historyCount = static_cast<uint8_t>(_historyCount),
                .trailingTarget =
                    static_cast<uint8_t>(ErrorJournalConfig::trailingRecords),
            },
            wakeWriter);

        const auto start =
            (_historyNext + ErrorJournalConfig::historyRecords -
             _historyCount) %
            ErrorJournalConfig::historyRecords;
        for (std::size_t i = 0; i < _historyCount; ++i) {
            _enqueueRecord(
                RecordTag::Pre,
                _history[(start + i) % ErrorJournalConfig::historyRecords],
                wakeWriter);
        }
        _enqueueRecord(RecordTag::Error, journalRecord, wakeWriter);

        slot->state = SlotState::Hot;
        slot->siteId = record.siteId;
        slot->openedAtMs = record.ts;
        slot->trailingCount = 0;
        ++_caught;
    }

    void _appendHistory(const LogRecord &record) {
        _history[_historyNext] = JournalRecord::from(record);
        _historyNext = (_historyNext + 1) % ErrorJournalConfig::historyRecords;
        if (_historyCount < ErrorJournalConfig::historyRecords) {
            ++_historyCount;
        }
    }

    [[nodiscard]] bool _siteSeen(LogSiteId siteId) const {
        for (std::size_t i = 0; i < _seenCount; ++i) {
            if (_seenSites[i] == siteId) {
                return true;
            }
        }
        return false;
    }

    bool _rememberSite(LogSiteId siteId) {
        if (_seenCount >= ErrorJournalConfig::seenSites) {
            return false;
        }
        _seenSites[_seenCount] = siteId;
        ++_seenCount;
        return true;
    }

    ErrorSlot *_findFreeSlot() {
        for (auto &slot : _slots) {
            if (slot.state == SlotState::Free) {
                return &slot;
            }
        }
        return nullptr;
    }

    void _closeExpiredSlots() {
        const auto now = ::platform::get_time();
        bool wakeWriter = false;
        {
            Mutex::ScopedSpinlockGuard guard{_lock};
            if (_disabled.load(std::memory_order_relaxed)) {
                return;
            }
            for (auto &slot : _slots) {
                if (slot.state != SlotState::Hot) {
                    continue;
                }
                if (static_cast<uint32_t>(now - slot.openedAtMs) <
                    this->config().hotTimeoutMs) {
                    continue;
                }
                _enqueueSeparator(wakeWriter);
                slot = ErrorSlot{};
            }
        }
        if (wakeWriter) {
            _wakeWriter();
        }
    }

    bool _enqueueHeader(const HeaderLine &line, bool &wakeWriter) {
        return _enqueue(JournalLine::makeHeader(line), wakeWriter);
    }

    bool _enqueueRecord(RecordTag tag, const JournalRecord &record,
                        bool &wakeWriter) {
        return _enqueue(JournalLine::makeRecord(tag, record), wakeWriter);
    }

    bool _enqueueSeparator(bool &wakeWriter) {
        return _enqueue(JournalLine::makeSeparator(), wakeWriter);
    }

    bool _enqueue(const JournalLine &line, bool &wakeWriter) {
        if (_queueCount >= ErrorJournalConfig::queueRecords) {
            ++_queueDrops;
            return false;
        }
        _queue[_queueWrite] = line;
        _queueWrite = (_queueWrite + 1) % ErrorJournalConfig::queueRecords;
        ++_queueCount;
        wakeWriter = true;
        return true;
    }

    [[nodiscard]] std::size_t _queueAvailable() const {
        return ErrorJournalConfig::queueRecords - _queueCount;
    }

    bool _popLine(JournalLine &line) {
        Mutex::ScopedSpinlockGuard guard{_lock};
        if (_disabled.load(std::memory_order_relaxed) || _queueCount == 0) {
            return false;
        }
        line = _queue[_queueRead];
        _queueRead = (_queueRead + 1) % ErrorJournalConfig::queueRecords;
        --_queueCount;
        return true;
    }

    void _drainQueue() {
        File file{};
        bool fileOpen = false;
        JournalLine line{};

        while (_popLine(line)) {
            if (!fileOpen) {
                if (!_openFile(file)) {
                    return;
                }
                fileOpen = true;
            }

            const auto ret = _writeLine(file, line);
            if (!ret.ok()) {
                (void)file.closeQuiet();
                _handleWriteFailure(ret);
                return;
            }
            _incrementWritten();
        }

        if (fileOpen) {
            auto flushRet = file.flushQuiet();
            auto closeRet = file.closeQuiet();
            if (!flushRet.ok()) {
                _handleWriteFailure(flushRet);
            } else if (!closeRet.ok()) {
                _handleWriteFailure(closeRet);
            }
        }
    }

    bool _openFile(File &file) {
        bool flashFull = false;
        if (!_storageWritable(&flashFull)) {
            _disableQuietly(flashFull);
            return false;
        }

        auto ret =
            FileSystemService::get().openAppendQuiet(file, this->config().path);
        if (!ret.ok()) {
            (void)file.closeQuiet();
            _handleWriteFailure(ret);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool _storageWritable(bool *flashFull = nullptr) {
        if (flashFull != nullptr) {
            *flashFull = false;
        }
        if (!FileSystemService::configured()) {
            return false;
        }

        auto &fileSystem = FileSystemService::get();
        if (!fileSystem.active()) {
            return false;
        }

        auto storageInfo = fileSystem.info();
        if (!storageInfo) {
            return false;
        }
        if (storageInfo->totalBytes <= storageInfo->usedBytes) {
            if (flashFull != nullptr) {
                *flashFull = true;
            }
            return false;
        }
        const auto freeBytes = storageInfo->totalBytes - storageInfo->usedBytes;
        if (freeBytes <= this->config().minFreeBytes) {
            if (flashFull != nullptr) {
                *flashFull = true;
            }
            return false;
        }
        return true;
    }

    ReturnCode _writeLine(File &file, const JournalLine &line) {
        switch (line.kind) {
        case LineKind::Header:
            return _writeFormatted(
                file,
                "--- site=#%08" PRIx32 " history=%u trailing=%u errorTs=%" PRIu32
                " synced=%" PRIu32 " ---\n",
                static_cast<uint32_t>(line.header.siteId),
                line.header.historyCount, line.header.trailingTarget,
                line.header.errorTs, line.header.errorTsSynced);
        case LineKind::Record:
            return _writeRecord(file, line.record.tag, line.record.record);
        case LineKind::Separator:
            return _writeRaw(file, "\n\n");
        }
        return OK();
    }

    ReturnCode _writeRecord(File &file, RecordTag tag,
                            const JournalRecord &record) {
        return _writeFormatted(
            file,
            "%s %s (%" PRIu32 ") (%" PRIu32 ") <%s> #%08" PRIx32 ": %s\n",
            _recordTagName(tag), log_level_to_string(record.level), record.ts,
            record.tsSynced, log_component_to_string(record.component),
            static_cast<uint32_t>(record.siteId), record.msg.data());
    }

    static const char *_recordTagName(RecordTag tag) {
        switch (tag) {
        case RecordTag::Pre:
            return "pre";
        case RecordTag::Error:
            return "err";
        case RecordTag::Next:
            return "nxt";
        }
        return "???";
    }

    template <typename... Args>
    ReturnCode _writeFormatted(File &file, const char *format, Args... args) {
        const auto count = std::snprintf(_lineBuffer.data(),
                                         _lineBuffer.size(), format, args...);
        if (count < 0) {
            return ERR(CoreError, OperationFailed);
        }

        auto length = static_cast<std::size_t>(count);
        if (length >= _lineBuffer.size()) {
            length = _lineBuffer.size() - 1;
            _lineBuffer[length - 1] = '\n';
            _lineBuffer[length] = '\0';
        }
        return _writeRaw(file, std::string_view{_lineBuffer.data(), length});
    }

    static ReturnCode _writeRaw(File &file, std::string_view data) {
        auto written = file.write(data);
        if (!written) {
            return written.error();
        }
        if (*written != data.size()) {
            return ERR(CoreError, InvalidSize);
        }
        return OK();
    }

    void _handleWriteFailure(ReturnCode ret) {
        bool flashFull = ret == ERR(CoreError, OutOfMemory);
        if (!flashFull) {
            bool storageFull = false;
            if (!_storageWritable(&storageFull) && storageFull) {
                flashFull = true;
            }
        }
        Mutex::ScopedSpinlockGuard guard{_lock};
        ++_writeFailures;
        _disableQuietlyLocked(flashFull);
    }

    void _disableQuietly(bool flashFull) {
        Mutex::ScopedSpinlockGuard guard{_lock};
        _disableQuietlyLocked(flashFull);
    }

    void _disableQuietlyLocked(bool flashFull) {
        if (!_disabled.load(std::memory_order_relaxed)) {
            _disabled.store(true, std::memory_order_release);
            if (flashFull) {
                ++_flashFull;
            }
        }
        for (auto &slot : _slots) {
            slot = ErrorSlot{};
        }
        _historyCount = 0;
        _historyNext = 0;
        _queueRead = 0;
        _queueWrite = 0;
        _queueCount = 0;
    }

    void _incrementWritten() {
        Mutex::ScopedSpinlockGuard guard{_lock};
        ++_written;
    }

    [[nodiscard]] MetricSnapshot _snapshotMetrics() const {
        MetricSnapshot snapshot{};
        Mutex::ScopedSpinlockGuard guard{_lock};
        snapshot.caught = _caught;
        snapshot.written = _written;
        snapshot.slotDrops = _slotDrops;
        snapshot.siteDrops = _siteDrops;
        snapshot.queueDrops = _queueDrops;
        snapshot.writeFailures = _writeFailures;
        snapshot.flashFull = _flashFull;
        snapshot.disabled = _disabled.load(std::memory_order_relaxed);
        for (const auto &slot : _slots) {
            if (slot.state != SlotState::Free) {
                ++snapshot.activeSlots;
            }
        }
        return snapshot;
    }

    void _publishMetrics() const {
        const auto snapshot = _snapshotMetrics();
        _metrics.setCaught(snapshot.caught);
        _metrics.setWritten(snapshot.written);
        _metrics.setSlotDrops(snapshot.slotDrops);
        _metrics.setSiteDrops(snapshot.siteDrops);
        _metrics.setQueueDrops(snapshot.queueDrops);
        _metrics.setWriteFailures(snapshot.writeFailures);
        _metrics.setFlashFull(snapshot.flashFull);
        _metrics.setDisabled(snapshot.disabled);
        _metrics.setActiveSlots(snapshot.activeSlots);
    }

    void _wakeWriter() {
        if (_task == 0) {
            return;
        }
        (void)this->_taskController.signalTaskDirect(_task, Signal::Ping);
    }

    mutable ::platform::Spinlock _lock = ::platform::create_spinlock();
    std::array<JournalRecord, ErrorJournalConfig::historyRecords> _history{};
    std::size_t _historyNext = 0;
    std::size_t _historyCount = 0;
    std::array<ErrorSlot, ErrorJournalConfig::errorSlots> _slots{};
    std::array<LogSiteId, ErrorJournalConfig::seenSites> _seenSites{};
    std::size_t _seenCount = 0;

    std::array<JournalLine, ErrorJournalConfig::queueRecords> _queue{};
    std::size_t _queueRead = 0;
    std::size_t _queueWrite = 0;
    std::size_t _queueCount = 0;

    std::array<char, ErrorJournalConfig::lineBufferSize> _lineBuffer{};

    uint32_t _caught = 0;
    uint32_t _written = 0;
    uint32_t _slotDrops = 0;
    uint32_t _siteDrops = 0;
    uint32_t _queueDrops = 0;
    uint32_t _writeFailures = 0;
    uint32_t _flashFull = 0;

    std::atomic_bool _active{false};
    std::atomic_bool _disabled{false};
    TaskController::RunnerKey _task = 0;
    JournalMetrics _metrics = JournalMetrics::create();
};

inline constexpr LifecycleContract<ErrorJournalSink, ErrorJournalConfig>
    _error_journal_lifecycle;
inline constexpr TaskControllerContract<ErrorJournalSink>
    _error_journal_task_controller;
inline constexpr TaskController::TaskHooks::Contract<ErrorJournalSink>
    _error_journal_task_hook;

} // namespace Totem::LoggingBackend::detail::Outputs
