#pragma once

#include "Base/HasLifecycle.hh"
#include "Macros/Facade.hh"
#include "Output/detail/Config.hh"
#include "Output/detail/PlatformSelect.hh"
#include "Output/detail/Types.hh"
#include "Types/Error.hh"
#include "Types/Logging.hh"
#include <array>
#include <cinttypes>
#include <cstdio>
#include <expected>

namespace Totem::Output::detail::Outputs {

class OutputUart : public HasLifecycle<OutputUart, UartConfig> {
    friend class HasLifecycle<OutputUart, UartConfig>;
    friend struct LifecycleContract<OutputUart, UartConfig>;

  public:
    static constexpr const char *name = "Output::Uart";

    std::expected<Sink, ReturnCode> sink() {
        FAIL_IF_INACTIVE_UNEXPECTED("Cannot get sink for inactive output");
        return Sink::bind(*this, true /*active*/);
    }

    ReturnCode write(const LogRecord &record) {
        std::array<char, maxFormattedSize> buf{};

        int num =
            std::snprintf(buf.data(), buf.size(), "%" PRIu32 " [%s] <%s>: %s\n",
                          record.ts, logLevelToString(record.level),
                          record.tag.data(), record.msg.data());

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

        return Platform::uart_write(buf.data(), static_cast<size_t>(num),
                                    config().flush);
    }

  private:
    ReturnCode _onBegin() { return Platform::uart_init(config()); }

    static ReturnCode _onEnd() { return Platform::uart_deinit(); }

    static constexpr size_t maxFormattedSize =
        10 + // Timestamp max length in characters (assuming 32-bit unsigned
             // int)
        2 +  // Space and bracket open
        logLevelLength + // Log level string max length
        3 +              // Bracket close, space, and angle bracket open
        logTagLength +   // Tag max size
        3 +              // Angle bracket close, colon, and space
        logMaxLength;    // Message max size

    using DefaultError = CoreError;
};

inline constexpr LifecycleContract<OutputUart, UartConfig>
    _output_uart_lifecycle;
inline constexpr Sink::Contract<OutputUart> _output_uart_sink;

} // namespace Totem::Output::detail::Outputs
