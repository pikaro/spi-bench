#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hh"
#include "CommandBackend/Interfaces/Transport.hh"
#include "Macros/Facade.hh"
#include "Platform/Uart.hh"
#include "StaticConfig/Command.hh"
#include "StaticConfig/Uart.hh"
#include "Types/Error.hh"
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>

namespace Totem::CommandBackend::detail::Transports {

class UartTransport {
  public:
    static constexpr const char *name = "Transport::Serial";

    std::expected<Transport, ReturnCode> transport() {
        return Transport::bind(*this);
    }

    std::expected<CommandDesc::Tokens, ReturnCode> poll() {
        _tokenCount = 0;

        auto readResult = platform::Uart::read(_rxChunk);
        if (!readResult) {
            if (readResult.error() == ERR(NotFound)) {
                return std::unexpected(ERR(NotFound));
            }

            FAIL(std::unexpected(readResult.error()),
                 "Failed to read from UART in %s", name);
        }
        const auto readBytes = *readResult;

        for (size_t i = 0; i < readBytes; ++i) {
            const char chr = static_cast<char>(_rxChunk[i]);

            if (chr == '\r') {
                continue;
            }

            if (chr == '\n') {
                return _finalizeLine();
            }

            if (_lineLen >= CommandConfig::maxLineLen) {
                _resetLine();
                FAIL(std::unexpected(ERR(CommandError, TooLong)),
                     "Received line exceeds maximum length in %s", name);
            }

            _line[_lineLen++] = chr;

            if (_lineLen >= 2 && _line[0] == '\x1b' && _line[1] == '[') {
                _handleEscapeSequence(chr);
                _resetLine();
                return std::unexpected(ERR(NotFound));
            }
        }

        return std::unexpected(ERR(NotFound));
    }

  private:
    std::expected<CommandDesc::Tokens, ReturnCode> _finalizeLine() {
        _line[_lineLen] = '\0';

        if (_lineLen == 0) {
            if (_pendingLastCommand) {
                _pendingLastCommand = false;

                if (!_hasLastCommand) {
                    return std::unexpected(ERR(NotFound));
                }

                std::memcpy(_line.data(), _lastCommand.data(), _lastCommandLen);
                _lineLen = _lastCommandLen;
                _line[_lineLen] = '\0';
            } else {
                return std::unexpected(ERR(NotFound));
            }
        }

        auto result = _tokenizeCurrentLine();
        if (result) {
            std::memcpy(_lastCommand.data(), _line.data(), _lineLen);
            _lastCommandLen = _lineLen;
            _lastCommand[_lastCommandLen] = '\0';
            _hasLastCommand = true;
        }

        _resetLine();
        return result;
    }

    std::expected<CommandDesc::Tokens, ReturnCode> _tokenizeCurrentLine() {
        FAIL_IF(_lineLen == 0, std::unexpected(ERR(CommandError, SyntaxError)),
                "Cannot tokenize empty line");
        FAIL_IF(_line[0] != '/',
                std::unexpected(ERR(CommandError, SyntaxError)),
                "Command line must start with '/'");

        _tokenCount = 0;

        size_t i = 1; // skip leading '/'

        while (i < _lineLen) {
            while (i < _lineLen &&
                   // Skip leading spaces
                   std::isspace(static_cast<unsigned char>(_line[i])) != 0) {
                ++i;
            }

            if (i >= _lineLen) {
                break;
            }

            const size_t start = i;

            while (i < _lineLen &&
                   // Consume non-space characters
                   std::isspace(static_cast<unsigned char>(_line[i])) == 0) {
                ++i;
            }

            FAIL_IF(_tokenCount >= CommandConfig::maxTokens,
                    std::unexpected(ERR(CommandError, TooLong)),
                    "Number of tokens in command line exceeds maximum in %s",
                    name);

            _tokens[_tokenCount++] =
                CommandDesc::Token{&_line[start], i - start};

            _log_d("Parsed token: " SV_FMT, SV_ARG(_tokens[_tokenCount - 1]));
        }

        _log_d("Total tokens parsed: %zu", _tokenCount);

        FAIL_IF(_tokenCount == 0,
                std::unexpected(ERR(CommandError, SyntaxError)),
                "No command found in line");

        return CommandDesc::Tokens{_tokens.data(), _tokenCount};
    }

    void _resetLine() { _lineLen = 0; }

    void _handleEscapeSequence(char lastChar) {
        switch (lastChar) {
        case 'A':
            if (_hasLastCommand) {
                _pendingLastCommand = true;
            }
            break;
        default:
            break;
        }
    }

    std::array<uint8_t, UartConfig::maxReadLen> _rxChunk{};
    std::array<char, CommandConfig::maxLineLen + 1> _line{};
    size_t _lineLen = 0;

    std::array<CommandDesc::Token, CommandConfig::maxTokens> _tokens{};
    size_t _tokenCount = 0;

    std::array<char, CommandConfig::maxLineLen + 1> _lastCommand{};
    size_t _lastCommandLen = 0;
    bool _hasLastCommand = false;
    bool _pendingLastCommand = false;
};

} // namespace Totem::CommandBackend::detail::Transports
