#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include "LoggingBackend/detail/PlatformSelect.hpp"
#include "Services/Logging.hpp"
#include "Types/Error.hpp"
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace Totem::LoggingBackend::detail {

class NativeLogBridge {
  public:
    static ReturnCode begin() {
        if (_installed.exchange(true, std::memory_order_acq_rel)) {
            return OK();
        }
        Platform::limitNativeLogLevel(_nativeLevelFor(_startupLevel()));
        _previous = Platform::setNativeLogVprintf(_write);
        return OK();
    }

    static ReturnCode end() {
        if (!_installed.exchange(false, std::memory_order_acq_rel)) {
            return OK();
        }

        auto restore =
            _previous != nullptr ? _previous : Platform::defaultNativeLogVprintf();
        auto previous = Platform::setNativeLogVprintf(restore);
        if (previous != _write) {
            (void)Platform::setNativeLogVprintf(previous);
        }
        _previous = nullptr;
        _resetLine();
        return OK();
    }

  private:
    static int _write(const char *format, va_list args) {
        if (format == nullptr) {
            return 0;
        }

        if (!_installed.load(std::memory_order_acquire) ||
            !LoggingService::backendConfigured() ||
            _busy.test_and_set(std::memory_order_acquire)) {
            return _writePrevious(format, args);
        }

        auto result = _writeLocked(format, args);
        _busy.clear(std::memory_order_release);
        return result;
    }

    static int _writeLocked(const char *format, va_list args) {
        if (_dropLine) {
            if (_formatEndsLine(format, args)) {
                _resetLine();
            }
            return 0;
        }

        const auto start = _lineLength;
        const auto remaining = _line.size() - _lineLength;

        va_list copy;
        va_copy(copy, args);
        auto formatted =
            std::vsnprintf(_line.data() + _lineLength, remaining, format, copy);
        va_end(copy);

        if (formatted < 0) {
            return formatted;
        }

        auto written = static_cast<size_t>(formatted);
        if (written >= remaining) {
            _lineLength = _line.size() - 1;
            _lineTruncated = true;
            _emitLine();
            return formatted;
        }

        _lineLength += written;
        _line[_lineLength] = '\0';

        _captureLineLevel();
        if (_lineLevelKnown &&
            !LoggingService::loggingFor(_lineLevel, LogComponent::Esp)) {
            _dropLine = true;
            _lineLength = 0;
            _line[0] = '\0';
            _lineTruncated = false;
            return formatted;
        }

        for (size_t i = start; i < _lineLength; ++i) {
            if (_line[i] == '\n') {
                _lineLength = i;
                if (_lineLength > 0 && _line[_lineLength - 1] == '\r') {
                    --_lineLength;
                }
                _line[_lineLength] = '\0';
                _emitLine();
                break;
            }
        }

        return formatted;
    }

    static int _writePrevious(const char *format, va_list args) {
        auto previous =
            _previous != nullptr ? _previous : Platform::defaultNativeLogVprintf();
        return previous(format, args);
    }

    static void _emitLine() {
        if (_lineLength == 0 && !_lineTruncated) {
            _resetLine();
            return;
        }

        if (_lineTruncated && _line.size() >= 4) {
            auto markerPos = _lineLength;
            if (markerPos > _line.size() - 4) {
                markerPos = _line.size() - 4;
            }
            _line[markerPos] = '.';
            _line[markerPos + 1] = '.';
            _line[markerPos + 2] = '.';
            _lineLength = markerPos + 3;
        }
        _line[_lineLength] = '\0';

        _captureLineLevel();
        if (LoggingService::loggingFor(_lineLevel, LogComponent::Esp)) {
            (void)LoggingService::logf(_lineLevel, LogComponent::Esp, "%s",
                                       _line.data());
        }
        _resetLine();
    }

    static void _captureLineLevel() {
        if (_lineLevelKnown) {
            return;
        }
        _lineLevel = _levelForLine(_line.data());
        _lineLevelKnown = true;
    }

    static LogLevel _levelForLine(const char *line) {
        if (line == nullptr || line[0] == '\0' || line[1] != ' ') {
            return LogLevel::Info;
        }

        switch (line[0]) {
        case 'E':
            return LogLevel::Error;
        case 'W':
            return LogLevel::Warning;
        case 'I':
            return LogLevel::Info;
        case 'D':
            return LogLevel::Debug;
        case 'V':
            return LogLevel::Verbose;
        default:
            return LogLevel::Info;
        }
    }

    static void _resetLine() {
        _lineLength = 0;
        _line[0] = '\0';
        _lineTruncated = false;
        _lineLevel = LogLevel::Info;
        _lineLevelKnown = false;
        _dropLine = false;
    }

    static bool _formatEndsLine(const char *format, va_list args) {
        if (format == nullptr) {
            return false;
        }
        if (format[0] == '\n') {
            return true;
        }
        if (std::strcmp(format, "%s") != 0) {
            return false;
        }

        va_list copy;
        va_copy(copy, args);
        const char *value = va_arg(copy, const char *);
        va_end(copy);
        if (value == nullptr) {
            return false;
        }
        return std::strchr(value, '\n') != nullptr;
    }

    static constexpr LogLevel _startupLevel() {
        auto level = LoggingDefaultLevel::esp.value_or(
            LoggingDefaultLevel::defaultLevel);
        auto minimum = logging_minimum_for(LogComponent::Esp);
        return static_cast<uint8_t>(minimum) > static_cast<uint8_t>(level)
                   ? minimum
                   : level;
    }

    static constexpr Platform::NativeLogLevel _nativeLevelFor(LogLevel level) {
        switch (level) {
        case LogLevel::Verbose:
            return ESP_LOG_VERBOSE;
        case LogLevel::Debug:
            return ESP_LOG_DEBUG;
        case LogLevel::Info:
            return ESP_LOG_INFO;
        case LogLevel::Warning:
            return ESP_LOG_WARN;
        case LogLevel::Error:
            return ESP_LOG_ERROR;
        case LogLevel::Off:
            return ESP_LOG_NONE;
        default:
            return ESP_LOG_INFO;
        }
    }

    static inline std::atomic<bool> _installed{false};
    static inline std::atomic_flag _busy = ATOMIC_FLAG_INIT;
    static inline Platform::NativeLogVprintf _previous = nullptr;
    static inline std::array<char, logMaxLength> _line{};
    static inline size_t _lineLength = 0;
    static inline bool _lineTruncated = false;
    static inline LogLevel _lineLevel = LogLevel::Info;
    static inline bool _lineLevelKnown = false;
    static inline bool _dropLine = false;
};

} // namespace Totem::LoggingBackend::detail
