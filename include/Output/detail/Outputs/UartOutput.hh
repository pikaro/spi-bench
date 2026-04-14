#pragma once

#include "Base/HasLifecycle.hh"
#include "Macros/Facade.hh"
#include "Output/Interfaces/Config.hh"
#include "Output/Interfaces/Sink.hh"
#include "Platform/Uart.hh"
#include "StaticConfig/Logging.hh"
#include "Types/Error.hh"
#include "Types/Logging.hh"
#include <array>
#include <cinttypes>
#include <cstdio>
#include <expected>

namespace Totem::Output::detail::Outputs {

struct Colors {
    static constexpr const char *reset = "\x1b[0m";
    static constexpr const char *black = "\x1b[30m";
    static constexpr const char *red = "\x1b[31m";
    static constexpr const char *green = "\x1b[32m";
    static constexpr const char *yellow = "\x1b[33m";
    static constexpr const char *blue = "\x1b[34m";
    static constexpr const char *magenta = "\x1b[35m";
    static constexpr const char *cyan = "\x1b[36m";
    static constexpr const char *white = "\x1b[37m";
    static constexpr const char *brightBlack = "\x1b[90m";
    static constexpr const char *brightRed = "\x1b[91m";
    static constexpr const char *brightGreen = "\x1b[92m";
    static constexpr const char *brightYellow = "\x1b[93m";
    static constexpr const char *brightBlue = "\x1b[94m";
    static constexpr const char *brightMagenta = "\x1b[95m";
    static constexpr const char *brightCyan = "\x1b[96m";
    static constexpr const char *brightWhite = "\x1b[97m";
};

constexpr const char *logLevelColor(LogLevel level) {
    switch (level) {
    case LogLevel::Verbose:
        return Colors::brightBlack;
    case LogLevel::Debug:
        return Colors::blue;
    case LogLevel::Info:
        return Colors::green;
    case LogLevel::Warning:
        return Colors::yellow;
    case LogLevel::Error:
        return Colors::red;
    default:
        return Colors::brightMagenta;
    }
}

constexpr const char *logMessageColor(LogLevel level) {
    switch (level) {
    case LogLevel::Verbose:
    case LogLevel::Debug:
        return Colors::brightBlack;
    case LogLevel::Info:
    case LogLevel::Warning:
        return Colors::brightWhite;
    case LogLevel::Error:
        return Colors::red;
    default:
        return Colors::brightMagenta;
    }
}

class UartOutput : public HasLifecycle<UartOutput, UartConfig> {
    friend class HasLifecycle<UartOutput, UartConfig>;
    friend struct LifecycleContract<UartOutput, UartConfig>;

  public:
    static constexpr const char *name = "Output::Uart";

    std::expected<Sink, ReturnCode> sink() {
        FAIL_IF_INACTIVE_UNEXPECTED("Cannot get sink for inactive output");
        return Sink::bind(*this, true /*active*/);
    }

    ReturnCode write(const LogRecord &record) {
        std::array<char, maxFormattedSize> buf{};

        static constexpr char logFmt[] =
            "%s%s%s (%s%" PRIu32 "%s) <%s%s%s>: %s%s%s\n";

        const char *rst = "";
        const char *tsColor = "";
        const char *tagColor = "";
        const char *lvColor = "";
        const char *msgColor = "";

        if (LoggingConfig::useColor) {
            rst = Colors::reset;
            tsColor = Colors::brightBlack;
            tagColor = Colors::brightBlue;
            lvColor = logLevelColor(record.level);
            msgColor = logMessageColor(record.level);
        }

        int num = std::snprintf(buf.data(), buf.size(), logFmt, lvColor,
                                log_level_to_string(record.level), rst, tsColor,
                                record.ts, rst, tagColor, record.tag.data(),
                                rst, msgColor, record.msg.data(), rst);

        if (num < 0) {
            return ERR(CoreError, OperationFailed);
        }

        if (num > static_cast<int>(buf.size())) {
            buf[buf.size() - 4] = '%';
            buf[buf.size() - 3] = '%';
            buf[buf.size() - 2] = '%';
            buf[buf.size() - 1] = '\0';
            num = static_cast<int>(buf.size() - 1);
        }

        return ::platform::Uart::write(buf.data(), static_cast<size_t>(num),
                                       config().flush);
    }

  private:
    static ReturnCode _onBegin() { return OK(); }
    static ReturnCode _onEnd() { return OK(); }

    static constexpr size_t maxFormattedSize =
        10 + // Timestamp max length in characters (assuming 32-bit unsigned
             // int)
        2 +  // Space and bracket open
        logLevelLength + // Log level string max length
        3 +              // Bracket close, space, and angle bracket open
        logTagLength +   // Tag max size
        3 +              // Angle bracket close, colon, and space
        logMaxLength +
        (LoggingConfig::useColor ? (4 * 2 * 5) : 0); // Message max size
};

inline constexpr LifecycleContract<UartOutput, UartConfig>
    _output_uart_lifecycle;
inline constexpr Sink::Contract<UartOutput> _output_uart_sink;

} // namespace Totem::Output::detail::Outputs
