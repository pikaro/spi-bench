#pragma once

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Totem::LedRender {

struct JsonValue {
    enum class Kind : uint8_t {
        Null,
        Bool,
        Number,
        String,
        Object,
        Array,
    };

    using Object = std::vector<std::pair<std::string, JsonValue>>;
    using Array = std::vector<JsonValue>;

    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text{};
    Object object{};
    Array array{};

    [[nodiscard]] const JsonValue *find(std::string_view key) const {
        if (kind != Kind::Object) {
            return nullptr;
        }
        for (const auto &[name, value] : object) {
            if (name == key) {
                return &value;
            }
        }
        return nullptr;
    }
};

class JsonParser {
  public:
    explicit JsonParser(std::string_view input) : _input(input) {}

    [[nodiscard]] std::optional<JsonValue> parse(std::string &error) {
        skipWhitespace();
        auto value = parseValue(error);
        if (!value) {
            return std::nullopt;
        }
        skipWhitespace();
        if (_pos != _input.size()) {
            error = "Unexpected trailing JSON content";
            return std::nullopt;
        }
        return value;
    }

  private:
    [[nodiscard]] std::optional<JsonValue> parseValue(std::string &error) {
        skipWhitespace();
        if (_pos >= _input.size()) {
            error = "Unexpected end of JSON";
            return std::nullopt;
        }

        const char c = _input[_pos];
        if (c == '{') {
            return parseObject(error);
        }
        if (c == '[') {
            return parseArray(error);
        }
        if (c == '"') {
            auto text = parseString(error);
            if (!text) {
                return std::nullopt;
            }
            JsonValue value{};
            value.kind = JsonValue::Kind::String;
            value.text = *text;
            return value;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c)) != 0) {
            return parseNumber(error);
        }
        if (consumeLiteral("true")) {
            JsonValue value{};
            value.kind = JsonValue::Kind::Bool;
            value.boolean = true;
            return value;
        }
        if (consumeLiteral("false")) {
            JsonValue value{};
            value.kind = JsonValue::Kind::Bool;
            value.boolean = false;
            return value;
        }
        if (consumeLiteral("null")) {
            return JsonValue{};
        }

        error = "Unexpected JSON token";
        return std::nullopt;
    }

    [[nodiscard]] std::optional<JsonValue> parseObject(std::string &error) {
        JsonValue value{};
        value.kind = JsonValue::Kind::Object;
        ++_pos;
        skipWhitespace();
        if (consume('}')) {
            return value;
        }
        while (true) {
            skipWhitespace();
            if (_pos >= _input.size() || _input[_pos] != '"') {
                error = "Expected object key";
                return std::nullopt;
            }
            auto key = parseString(error);
            if (!key) {
                return std::nullopt;
            }
            skipWhitespace();
            if (!consume(':')) {
                error = "Expected ':' after object key";
                return std::nullopt;
            }
            auto member = parseValue(error);
            if (!member) {
                return std::nullopt;
            }
            value.object.emplace_back(*key, *member);
            skipWhitespace();
            if (consume('}')) {
                return value;
            }
            if (!consume(',')) {
                error = "Expected ',' or '}' in object";
                return std::nullopt;
            }
        }
    }

    [[nodiscard]] std::optional<JsonValue> parseArray(std::string &error) {
        JsonValue value{};
        value.kind = JsonValue::Kind::Array;
        ++_pos;
        skipWhitespace();
        if (consume(']')) {
            return value;
        }
        while (true) {
            auto entry = parseValue(error);
            if (!entry) {
                return std::nullopt;
            }
            value.array.push_back(*entry);
            skipWhitespace();
            if (consume(']')) {
                return value;
            }
            if (!consume(',')) {
                error = "Expected ',' or ']' in array";
                return std::nullopt;
            }
        }
    }

    [[nodiscard]] std::optional<std::string> parseString(std::string &error) {
        if (!consume('"')) {
            error = "Expected string";
            return std::nullopt;
        }
        std::string out{};
        while (_pos < _input.size()) {
            const char c = _input[_pos++];
            if (c == '"') {
                return out;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (_pos >= _input.size()) {
                error = "Unterminated escape sequence";
                return std::nullopt;
            }
            const char escaped = _input[_pos++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                out.push_back(escaped);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            default:
                error = "Unsupported JSON escape sequence";
                return std::nullopt;
            }
        }
        error = "Unterminated string";
        return std::nullopt;
    }

    [[nodiscard]] std::optional<JsonValue> parseNumber(std::string &error) {
        const size_t start = _pos;
        if (_input[_pos] == '-') {
            ++_pos;
        }
        while (_pos < _input.size() &&
               std::isdigit(static_cast<unsigned char>(_input[_pos])) != 0) {
            ++_pos;
        }
        if (_pos < _input.size() && _input[_pos] == '.') {
            ++_pos;
            while (_pos < _input.size() &&
                   std::isdigit(static_cast<unsigned char>(_input[_pos])) !=
                       0) {
                ++_pos;
            }
        }
        if (_pos < _input.size() &&
            (_input[_pos] == 'e' || _input[_pos] == 'E')) {
            ++_pos;
            if (_pos < _input.size() &&
                (_input[_pos] == '+' || _input[_pos] == '-')) {
                ++_pos;
            }
            while (_pos < _input.size() &&
                   std::isdigit(static_cast<unsigned char>(_input[_pos])) !=
                       0) {
                ++_pos;
            }
        }

        std::string token{_input.substr(start, _pos - start)};
        char *end = nullptr;
        const double number = std::strtod(token.c_str(), &end);
        if (end == token.c_str() || *end != '\0' || !std::isfinite(number)) {
            error = "Invalid JSON number";
            return std::nullopt;
        }
        JsonValue value{};
        value.kind = JsonValue::Kind::Number;
        value.number = number;
        return value;
    }

    void skipWhitespace() {
        while (_pos < _input.size() &&
               std::isspace(static_cast<unsigned char>(_input[_pos])) != 0) {
            ++_pos;
        }
    }

    [[nodiscard]] bool consume(char expected) {
        if (_pos < _input.size() && _input[_pos] == expected) {
            ++_pos;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool consumeLiteral(std::string_view literal) {
        if (_input.substr(_pos, literal.size()) == literal) {
            _pos += literal.size();
            return true;
        }
        return false;
    }

    std::string_view _input;
    size_t _pos = 0;
};

[[nodiscard]] inline std::string jsonTypeName(JsonValue::Kind kind) {
    switch (kind) {
    case JsonValue::Kind::Null:
        return "null";
    case JsonValue::Kind::Bool:
        return "bool";
    case JsonValue::Kind::Number:
        return "number";
    case JsonValue::Kind::String:
        return "string";
    case JsonValue::Kind::Object:
        return "object";
    case JsonValue::Kind::Array:
        return "array";
    }
    return "unknown";
}

template <typename T>
[[nodiscard]] bool readJsonInteger(const JsonValue &value, T &out,
                                   std::string &error, std::string_view field) {
    static_assert(std::is_integral_v<T>, "integer field required");
    if (value.kind != JsonValue::Kind::Number) {
        error = "Field '" + std::string(field) + "' expected number, got " +
                jsonTypeName(value.kind);
        return false;
    }
    const auto rounded = std::llround(value.number);
    if (std::abs(value.number - static_cast<double>(rounded)) > 0.000001) {
        error = "Field '" + std::string(field) + "' expected integer";
        return false;
    }
    if constexpr (std::is_unsigned_v<T>) {
        if (rounded < 0 || static_cast<unsigned long long>(rounded) >
                               static_cast<unsigned long long>(
                                   std::numeric_limits<T>::max())) {
            error = "Field '" + std::string(field) + "' out of range";
            return false;
        }
    } else {
        if (rounded < static_cast<long long>(std::numeric_limits<T>::min()) ||
            rounded > static_cast<long long>(std::numeric_limits<T>::max())) {
            error = "Field '" + std::string(field) + "' out of range";
            return false;
        }
    }
    out = static_cast<T>(rounded);
    return true;
}

[[nodiscard]] inline bool readJsonString(const JsonValue &value,
                                         std::string &out, std::string &error,
                                         std::string_view field) {
    if (value.kind != JsonValue::Kind::String) {
        error = "Field '" + std::string(field) + "' expected string, got " +
                jsonTypeName(value.kind);
        return false;
    }
    out = value.text;
    return true;
}

[[nodiscard]] inline bool readJsonBool(const JsonValue &value, bool &out,
                                       std::string &error,
                                       std::string_view field) {
    if (value.kind != JsonValue::Kind::Bool) {
        error = "Field '" + std::string(field) + "' expected bool, got " +
                jsonTypeName(value.kind);
        return false;
    }
    out = value.boolean;
    return true;
}

[[nodiscard]] inline std::string escapeJson(std::string_view value) {
    std::ostringstream out;
    for (const char c : value) {
        switch (c) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            out << c;
            break;
        }
    }
    return out.str();
}

} // namespace Totem::LedRender
