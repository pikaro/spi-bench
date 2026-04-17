#pragma once

#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "Output/Interfaces/Config.hpp"
#include "Output/Interfaces/Sink.hpp"
#include "Platform/Uart.hpp"
#include "StaticConfig/Logging.hpp"
#include "Types/Basic.hpp"
#include "Types/Error.hpp"
#include "Types/Logging.hpp"
#include <array>
#include <cinttypes>
#include <cstdio>
#include <expected>

namespace Totem::Output::detail::Outputs {

constexpr Color log_level_color(LogLevel level) {
    switch (level) {
    case LogLevel::Verbose:
        return Color::BrightBlack;
    case LogLevel::Debug:
        return Color::Blue;
    case LogLevel::Info:
        return Color::Green;
    case LogLevel::Warning:
        return Color::Yellow;
    case LogLevel::Error:
        return Color::Red;
    default:
        return Color::BrightMagenta;
    }
}

constexpr Color log_message_color(LogLevel level) {
    switch (level) {
    case LogLevel::Verbose:
    case LogLevel::Debug:
        return Color::BrightBlack;
    case LogLevel::Info:
    case LogLevel::Warning:
        return Color::BrightWhite;
    case LogLevel::Error:
        return Color::Red;
    default:
        return Color::BrightMagenta;
    }
}

constexpr const char *log_color_to_string(Color color) {
    switch (color) {
    case Color::None:
        return "";
    case Color::Reset:
        return "\033[0m";
    case Color::Black:
        return "\033[30m";
    case Color::Red:
        return "\033[31m";
    case Color::Green:
        return "\033[32m";
    case Color::Yellow:
        return "\033[33m";
    case Color::Blue:
        return "\033[34m";
    case Color::Magenta:
        return "\033[35m";
    case Color::Cyan:
        return "\033[36m";
    case Color::White:
        return "\033[37m";
    case Color::BrightBlack:
        return "\033[90m";
    case Color::BrightRed:
        return "\033[91m";
    case Color::BrightGreen:
        return "\033[92m";
    case Color::BrightYellow:
        return "\033[93m";
    case Color::BrightBlue:
        return "\033[94m";
    case Color::BrightMagenta:
        return "\033[95m";
    case Color::BrightCyan:
        return "\033[96m";
    case Color::BrightWhite:
        return "\033[97m";
    default:
        return "";
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

        Color rst = Color::None;
        Color tsColor = Color::None;
        Color tagColor = Color::None;
        Color lvColor = Color::None;
        Color msgColor = Color::None;

        if (LoggingConfig::useColor) {
            rst = Color::Reset;
            tsColor = Color::BrightBlack;
            tagColor = log_component_to_color(record.component);
            lvColor = log_level_color(record.level);
            msgColor = log_message_color(record.level);
        }

        const auto *rstS = log_color_to_string(rst);
        const auto *tsColorS = log_color_to_string(tsColor);
        const auto *tagColorS = log_color_to_string(tagColor);
        const auto *lvColorS = log_color_to_string(lvColor);
        const auto *msgColorS = log_color_to_string(msgColor);

        int num = std::snprintf(buf.data(), buf.size(), logFmt, lvColorS,
                                log_level_to_string(record.level), rstS,
                                tsColorS, record.ts, rstS, tagColorS,
                                log_component_to_string(record.component), rstS,
                                msgColorS, record.msg.data(), rstS);

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
