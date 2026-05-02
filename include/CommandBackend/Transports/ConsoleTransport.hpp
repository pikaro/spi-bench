#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/Interfaces/ITransport.hpp"
#include "CommandBackend/detail/Store.hpp"
#include "CommandBackend/detail/Types.hpp" // IWYU pragma: keep
#include "Macros/Facade.hpp"
#include "Platform/Console.hpp"
#include "StaticConfig/Command.hpp"
#include "StaticConfig/Console.hpp"
#include "Types/Error.hpp"
#include "Types/Signal.hpp"
#include "Types/Uart.hpp"
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <expected>
#include <span>
#include <string_view>

namespace Totem::CommandBackend::detail::Transports {

class ConsoleTransport : public ITransport {
  public:
    using WakeCallback = ReturnCode (*)(void *, Signal);

    static constexpr const char *name = "Transport::Serial";

    ConsoleTransport() = default;
    explicit ConsoleTransport(const Store &store) : _store(&store) {}
    ConsoleTransport(const Store &store, void *wakeOwner,
                     WakeCallback wakeCallback)
        : _store(&store), _wakeOwner(wakeOwner), _wakeCallback(wakeCallback) {}

    [[nodiscard]] std::string_view displayName() const override { return name; }

    ReturnCode begin() {
        if (_wakeCallback == nullptr) {
            return OK();
        }
        return platform::Console::registerCallback(this, _onConsoleEvent);
    }

    std::expected<CommandDesc::Tokens, ReturnCode> poll() override {
        _tokenCount = 0;

        auto readResult = platform::Console::read(_rxChunk);

        if (!readResult) {
            if (readResult.error() == ERR(NotFound)) {
                return std::unexpected(ERR(NotFound));
            }

            FAIL(std::unexpected(readResult.error()),
                 "Failed to read from Console in %s", name);
        }
        const auto readBytes = *readResult;

        for (size_t i = 0; i < readBytes; ++i) {
            auto result = _handleInputByte(static_cast<char>(_rxChunk[i]));
            if (result) {
                return result;
            }
            if (result.error() != ERR(NotFound)) {
                return result;
            }
        }

        return std::unexpected(ERR(NotFound));
    }

  private:
    enum class EscapeState { None, Escape, Csi };
    enum class CompletionKind { None, RootCommand, Subcommand };

    struct CompletionTarget {
        CompletionKind kind = CompletionKind::None;
        size_t tokenStart = 0;
        std::string_view prefix{};
        std::span<const CommandDesc> subcommands{};
        bool prependSlash = false;
    };

    struct CompletionScan {
        size_t matchCount = 0;
        std::string_view firstMatch{};
        size_t commonPrefixLen = 0;
    };

    static constexpr LogComponent logComponent =
        Totem::CommandBackend::detail::logComponent;
    static constexpr size_t historyCapacity = 5;
    static constexpr size_t noHistoryCursor = historyCapacity;
    static constexpr std::string_view clearLine = "\r\033[2K";

    static ReturnCode _onConsoleEvent(void *owner, UartEvent event) {
        auto *self = static_cast<ConsoleTransport *>(owner);
        switch (event.type) {
        case UartEventType::Data:
            return self->_wake(Signal::UartData);
        case UartEventType::Overflow:
            return self->_wake(Signal::UartOverflow);
        case UartEventType::Error:
        case UartEventType::Break:
        case UartEventType::Pattern:
        case UartEventType::Unknown:
        default:
            return self->_wake(Signal::UartError);
        }
    }

    ReturnCode _wake(Signal signal) {
        if (_wakeCallback == nullptr) {
            return OK();
        }

        auto ret = _wakeCallback(_wakeOwner, signal);
        if (ret == ERR(InvalidState)) {
            return OK();
        }
        return ret;
    }

    std::expected<CommandDesc::Tokens, ReturnCode>
    _handleInputByte(char chr) {
        if (chr == '\n' && _dropNextLf) {
            _dropNextLf = false;
            return std::unexpected(ERR(NotFound));
        }
        _dropNextLf = false;

        if (_escapeState != EscapeState::None) {
            return _handleEscapeByte(chr);
        }

        switch (chr) {
        case '\x1b':
            _escapeState = EscapeState::Escape;
            return std::unexpected(ERR(NotFound));
        case '\r':
            _dropNextLf = true;
            return _finalizeLine();
        case '\n':
            return _finalizeLine();
        case '\b':
        case '\x7f':
            return _backspace();
        case '\t':
            return _complete();
        case '\x03': // Ctrl-C
            return _cancelLine();
        case '\x0c': // Ctrl-L
            return _clearScreen();
        case '\x15': // Ctrl-U
            return _clearInputLine();
        default:
            break;
        }

        if (std::isprint(static_cast<unsigned char>(chr)) != 0) {
            return _appendInputChar(chr);
        }

        return std::unexpected(ERR(NotFound));
    }

    std::expected<CommandDesc::Tokens, ReturnCode>
    _handleEscapeByte(char chr) {
        if (_escapeState == EscapeState::Escape) {
            if (chr == '[') {
                _escapeState = EscapeState::Csi;
                return std::unexpected(ERR(NotFound));
            }

            _escapeState = EscapeState::None;
            return std::unexpected(ERR(NotFound));
        }

        if ((chr >= '0' && chr <= '9') || chr == ';') {
            return std::unexpected(ERR(NotFound));
        }

        _escapeState = EscapeState::None;
        switch (chr) {
        case 'A':
            FAIL_IF_ERR_FWD_UNEXPECTED(_historyPrevious(),
                                       "Failed to show previous command");
            break;
        case 'B':
            FAIL_IF_ERR_FWD_UNEXPECTED(_historyNext(),
                                       "Failed to show next command");
            break;
        default:
            break;
        }

        return std::unexpected(ERR(NotFound));
    }

    std::expected<CommandDesc::Tokens, ReturnCode> _appendInputChar(char chr) {
        FAIL_IF_ERR_FWD_UNEXPECTED(_appendToLine(std::string_view{&chr, 1}),
                                   "Failed to append console input");
        _leaveHistoryBrowse();
        return _redrawAndContinue();
    }

    std::expected<CommandDesc::Tokens, ReturnCode> _backspace() {
        if (_lineLen > 0) {
            --_lineLen;
            _line[_lineLen] = '\0';
            _leaveHistoryBrowse();
        }
        return _redrawAndContinue();
    }

    std::expected<CommandDesc::Tokens, ReturnCode> _clearInputLine() {
        _resetLine();
        return _redrawAndContinue();
    }

    std::expected<CommandDesc::Tokens, ReturnCode> _cancelLine() {
        _resetLine();
        FAIL_IF_ERR_FWD_UNEXPECTED(_writeString("^C\r\n"),
                                   "Failed to echo console cancel");
        return _redrawAndContinue();
    }

    std::expected<CommandDesc::Tokens, ReturnCode> _clearScreen() {
        FAIL_IF_ERR_FWD_UNEXPECTED(_writeString("\033[2J\033[H"),
                                   "Failed to clear console screen");
        return _redrawAndContinue();
    }

    std::expected<CommandDesc::Tokens, ReturnCode> _complete() {
        FAIL_IF_ERR_FWD_UNEXPECTED(_completeLine(),
                                   "Failed to complete console input");
        return _redrawAndContinue();
    }

    std::expected<CommandDesc::Tokens, ReturnCode> _redrawAndContinue() {
        FAIL_IF_ERR_FWD_UNEXPECTED(_redrawLine(),
                                   "Failed to redraw console input");
        return std::unexpected(ERR(NotFound));
    }

    std::expected<CommandDesc::Tokens, ReturnCode> _finalizeLine() {
        FAIL_IF_ERR_FWD_UNEXPECTED(_writeString("\r\n"),
                                   "Failed to echo console newline");
        _line[_lineLen] = '\0';

        if (_lineLen == 0) {
            _resetLine();
            return std::unexpected(ERR(NotFound));
        }

        auto result = _tokenizeCurrentLine();
        if (result) {
            _pushHistory(std::string_view{_line.data(), _lineLen});
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

    void _resetLine() {
        _lineLen = 0;
        _historyCursor = noHistoryCursor;
    }

    void _leaveHistoryBrowse() { _historyCursor = noHistoryCursor; }

    ReturnCode _appendToLine(std::string_view text) {
        if (_lineLen + text.size() > CommandConfig::maxLineLen) {
            _resetLine();
            FAIL(ERR(CommandError, TooLong),
                 "Received line exceeds maximum length in %s", name);
        }

        if (!text.empty()) {
            std::memcpy(&_line[_lineLen], text.data(), text.size());
            _lineLen += text.size();
        }
        _line[_lineLen] = '\0';
        return OK();
    }

    ReturnCode _writeString(std::string_view text) {
        if (text.empty()) {
            return OK();
        }
        return platform::Console::write(
            std::as_bytes(std::span<const char>(text.data(), text.size())),
            true);
    }

    ReturnCode _redrawLine() {
        FAIL_IF_ERR_FWD(_writeString(clearLine),
                        "Failed to clear console line");
        return _writeString(std::string_view{_line.data(), _lineLen});
    }

    void _copyLineFrom(std::string_view line) {
        std::memcpy(_line.data(), line.data(), line.size());
        _lineLen = line.size();
        _line[_lineLen] = '\0';
    }

    void _saveDraftLine() {
        std::memcpy(_draftLine.data(), _line.data(), _lineLen);
        _draftLineLen = _lineLen;
        _draftLine[_draftLineLen] = '\0';
    }

    ReturnCode _historyPrevious() {
        if (_historyCount == 0) {
            return _redrawLine();
        }

        if (_historyCursor == noHistoryCursor) {
            _saveDraftLine();
            _historyCursor = 0;
        } else if (_historyCursor + 1 < _historyCount) {
            ++_historyCursor;
        }

        _copyLineFrom(std::string_view{_history[_historyCursor].data(),
                                       _historyLens[_historyCursor]});
        return _redrawLine();
    }

    ReturnCode _historyNext() {
        if (_historyCursor == noHistoryCursor) {
            return _redrawLine();
        }

        if (_historyCursor > 0) {
            --_historyCursor;
            _copyLineFrom(std::string_view{_history[_historyCursor].data(),
                                           _historyLens[_historyCursor]});
        } else {
            _copyLineFrom(
                std::string_view{_draftLine.data(), _draftLineLen});
            _historyCursor = noHistoryCursor;
        }

        return _redrawLine();
    }

    bool _historyEntryEquals(size_t index, std::string_view line) const {
        return _historyLens[index] == line.size() &&
               std::memcmp(_history[index].data(), line.data(), line.size()) ==
                   0;
    }

    void _pushHistory(std::string_view line) {
        if (line.empty() ||
            (_historyCount > 0 && _historyEntryEquals(0, line))) {
            return;
        }

        const size_t last =
            _historyCount < historyCapacity ? _historyCount
                                            : historyCapacity - 1;
        for (size_t i = last; i > 0; --i) {
            _history[i] = _history[i - 1];
            _historyLens[i] = _historyLens[i - 1];
        }

        std::memcpy(_history[0].data(), line.data(), line.size());
        _historyLens[0] = line.size();
        _history[0][_historyLens[0]] = '\0';
        if (_historyCount < historyCapacity) {
            ++_historyCount;
        }
    }

    ReturnCode _completeLine() {
        auto target = _completionTarget();
        switch (target.kind) {
        case CompletionKind::RootCommand:
            return _completeRootCommand(target);
        case CompletionKind::Subcommand:
            return _completeSubcommand(target);
        default:
            return OK();
        }
    }

    CompletionTarget _completionTarget() const {
        std::array<std::string_view, CommandConfig::maxTokens> tokens{};
        std::array<size_t, CommandConfig::maxTokens> starts{};
        size_t tokenCount = 0;

        const bool hasSlash = _lineLen > 0 && _line[0] == '/';
        size_t i = hasSlash ? 1 : 0;
        while (i < _lineLen) {
            while (i < _lineLen &&
                   std::isspace(static_cast<unsigned char>(_line[i])) != 0) {
                ++i;
            }
            if (i >= _lineLen) {
                break;
            }

            if (tokenCount >= CommandConfig::maxTokens) {
                return {};
            }

            const size_t start = i;
            while (i < _lineLen &&
                   std::isspace(static_cast<unsigned char>(_line[i])) == 0) {
                ++i;
            }

            starts[tokenCount] = start;
            tokens[tokenCount++] =
                std::string_view{&_line[start], i - start};
        }

        const bool trailingWhitespace =
            _lineLen > 0 &&
            std::isspace(static_cast<unsigned char>(_line[_lineLen - 1])) != 0;
        size_t tokenIndex = 0;
        size_t tokenStart = hasSlash ? 1 : 0;
        std::string_view prefix{};

        if (trailingWhitespace) {
            tokenIndex = tokenCount;
            tokenStart = _lineLen;
        } else if (tokenCount > 0) {
            tokenIndex = tokenCount - 1;
            tokenStart = starts[tokenIndex];
            prefix = tokens[tokenIndex];
        }

        if (tokenIndex == 0) {
            return CompletionTarget{
                .kind = CompletionKind::RootCommand,
                .tokenStart = hasSlash ? tokenStart : 0,
                .prefix = prefix,
                .subcommands = {},
                .prependSlash = !hasSlash,
            };
        }

        if (!hasSlash || _store == nullptr || tokenCount == 0 ||
            tokens[0].size() > CommandConfig::maxNameLength) {
            return {};
        }

        const auto commandKey = CommandNameKey::fromStringView(tokens[0]);
        auto containsResult = _store->contains(commandKey);
        if (!containsResult || !*containsResult) {
            return {};
        }

        auto commandResult = _store->get(commandKey);
        if (!commandResult) {
            return {};
        }

        auto subcommands = commandResult->second.subcommands;
        for (size_t subcommandIndex = 1; subcommandIndex < tokenIndex;
             ++subcommandIndex) {
            const auto *subcommand =
                _findSubcommand(subcommands, tokens[subcommandIndex]);
            if (subcommand == nullptr) {
                return {};
            }
            subcommands = subcommand->subcommands;
        }

        if (subcommands.empty()) {
            return {};
        }

        return CompletionTarget{
            .kind = CompletionKind::Subcommand,
            .tokenStart = tokenStart,
            .prefix = prefix,
            .subcommands = subcommands,
        };
    }

    ReturnCode _completeRootCommand(const CompletionTarget &target) {
        if (_store == nullptr) {
            return OK();
        }

        FAIL_IF_UNEXPECTED_FWD(keys, _store->snapshotCommandKeys(),
                               "Failed to snapshot command names");

        CompletionScan scan{};
        for (size_t i = 0; i < keys.count; ++i) {
            _scanCompletion(scan, keys.keys[i].view(), target.prefix);
        }

        if (scan.matchCount == 0) {
            return OK();
        }
        if (scan.matchCount == 1) {
            return _applyCompletion(target, scan.firstMatch, true);
        }
        if (scan.commonPrefixLen > target.prefix.size()) {
            return _applyCompletion(
                target, scan.firstMatch.substr(0, scan.commonPrefixLen), false);
        }

        return _printRootCompletionMatches(keys, target.prefix);
    }

    ReturnCode _completeSubcommand(const CompletionTarget &target) {
        CompletionScan scan{};
        for (const auto &subcommand : target.subcommands) {
            _scanCompletion(scan, subcommand.name, target.prefix);
        }

        if (scan.matchCount == 0) {
            return OK();
        }
        if (scan.matchCount == 1) {
            return _applyCompletion(target, scan.firstMatch, true);
        }
        if (scan.commonPrefixLen > target.prefix.size()) {
            return _applyCompletion(
                target, scan.firstMatch.substr(0, scan.commonPrefixLen), false);
        }

        return _printSubcommandCompletionMatches(target);
    }

    static const CommandDesc *
    _findSubcommand(std::span<const CommandDesc> subcommands,
                    std::string_view name) {
        for (const auto &subcommand : subcommands) {
            if (subcommand.name == name) {
                return &subcommand;
            }
        }
        return nullptr;
    }

    static bool _startsWith(std::string_view value, std::string_view prefix) {
        return value.size() >= prefix.size() &&
               value.substr(0, prefix.size()) == prefix;
    }

    static size_t _commonPrefixLength(std::string_view lhs,
                                      std::string_view rhs) {
        size_t len = 0;
        while (len < lhs.size() && len < rhs.size() && lhs[len] == rhs[len]) {
            ++len;
        }
        return len;
    }

    static void _scanCompletion(CompletionScan &scan,
                                std::string_view candidate,
                                std::string_view prefix) {
        if (!_startsWith(candidate, prefix)) {
            return;
        }

        if (scan.matchCount == 0) {
            scan.firstMatch = candidate;
            scan.commonPrefixLen = candidate.size();
        } else {
            scan.commonPrefixLen =
                _commonPrefixLength(scan.firstMatch.substr(0,
                                                           scan.commonPrefixLen),
                                    candidate);
        }
        ++scan.matchCount;
    }

    ReturnCode _applyCompletion(const CompletionTarget &target,
                                std::string_view completion,
                                bool appendSpace) {
        _lineLen = target.tokenStart;
        if (target.prependSlash) {
            FAIL_IF_ERR_FWD(_appendToLine("/"),
                            "Failed to append command prefix");
        }
        FAIL_IF_ERR_FWD(_appendToLine(completion),
                        "Failed to append command completion");
        if (appendSpace) {
            FAIL_IF_ERR_FWD(_appendToLine(" "),
                            "Failed to append command completion separator");
        }
        _leaveHistoryBrowse();
        return OK();
    }

    ReturnCode _printRootCompletionMatches(
        const Store::CommandKeySnapshot &keys, std::string_view prefix) {
        FAIL_IF_ERR_FWD(_writeString("\r\n"),
                        "Failed to start command completion list");
        size_t printed = 0;
        for (size_t i = 0; i < keys.count; ++i) {
            const auto candidate = keys.keys[i].view();
            if (!_startsWith(candidate, prefix)) {
                continue;
            }
            if (printed++ > 0) {
                FAIL_IF_ERR_FWD(_writeString("  "),
                                "Failed to print command completion gap");
            }
            FAIL_IF_ERR_FWD(_writeString("/"),
                            "Failed to print command completion prefix");
            FAIL_IF_ERR_FWD(_writeString(candidate),
                            "Failed to print command completion");
        }
        return _writeString("\r\n");
    }

    ReturnCode
    _printSubcommandCompletionMatches(const CompletionTarget &target) {
        FAIL_IF_ERR_FWD(_writeString("\r\n"),
                        "Failed to start subcommand completion list");
        size_t printed = 0;
        for (const auto &subcommand : target.subcommands) {
            if (!_startsWith(subcommand.name, target.prefix)) {
                continue;
            }
            if (printed++ > 0) {
                FAIL_IF_ERR_FWD(_writeString("  "),
                                "Failed to print subcommand completion gap");
            }
            FAIL_IF_ERR_FWD(_writeString(subcommand.name),
                            "Failed to print subcommand completion");
        }
        return _writeString("\r\n");
    }

    std::array<std::byte, UartConsoleConfig.maxReadLen> _rxChunk{};
    std::array<char, CommandConfig::maxLineLen + 1> _line{};
    size_t _lineLen = 0;

    std::array<CommandDesc::Token, CommandConfig::maxTokens> _tokens{};
    size_t _tokenCount = 0;

    const Store *_store = nullptr;
    void *_wakeOwner = nullptr;
    WakeCallback _wakeCallback = nullptr;
    std::array<std::array<char, CommandConfig::maxLineLen + 1>,
               historyCapacity>
        _history{};
    std::array<size_t, historyCapacity> _historyLens{};
    size_t _historyCount = 0;
    size_t _historyCursor = noHistoryCursor;
    std::array<char, CommandConfig::maxLineLen + 1> _draftLine{};
    size_t _draftLineLen = 0;
    EscapeState _escapeState = EscapeState::None;
    bool _dropNextLf = false;
};

} // namespace Totem::CommandBackend::detail::Transports
