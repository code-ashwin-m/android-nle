#include "util/Json.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace nle {

namespace {
const JsonValue& NullInstance() {
    static const JsonValue kNull;
    return kNull;
}
}  // namespace

const JsonValue& JsonValue::Get(const std::string& key) const {
    if (type_ != JsonType::Object) return NullInstance();
    for (auto& [k, v] : object_) {
        if (k == key) return v;
    }
    return NullInstance();
}

bool JsonValue::Has(const std::string& key) const {
    if (type_ != JsonType::Object) return false;
    for (auto& [k, v] : object_) {
        if (k == key) return true;
    }
    return false;
}

// Hand-rolled recursive-descent parser. Kept in one translation unit and
// entirely private to Json.cpp -- callers only ever see JsonValue::Parse.
class JsonParser {
public:
    JsonParser(const std::string& text, std::string* error) : text_(text), error_(error) {}

    JsonValue ParseDocument() {
        SkipWhitespace();
        JsonValue v = ParseValue();
        if (failed_) return JsonValue();
        SkipWhitespace();
        if (pos_ != text_.size()) {
            Fail("trailing content after JSON value");
            return JsonValue();
        }
        return v;
    }

private:
    void Fail(const std::string& message) {
        if (failed_) return;  // keep the first error, it's usually the most useful
        failed_ = true;
        if (error_) *error_ = message + " (at byte " + std::to_string(pos_) + ")";
    }

    void SkipWhitespace() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) {
            pos_++;
        }
    }

    char Peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

    bool Consume(char expected) {
        if (Peek() != expected) return false;
        pos_++;
        return true;
    }

    bool ConsumeLiteral(const char* literal) {
        size_t len = std::char_traits<char>::length(literal);
        if (text_.compare(pos_, len, literal) != 0) return false;
        pos_ += len;
        return true;
    }

    JsonValue ParseValue() {
        SkipWhitespace();
        if (failed_) return JsonValue();
        switch (Peek()) {
            case '{':
                return ParseObject();
            case '[':
                return ParseArray();
            case '"':
                return ParseString();
            case 't':
            case 'f':
                return ParseBool();
            case 'n':
                return ParseNull();
            default:
                return ParseNumber();
        }
    }

    JsonValue ParseObject() {
        JsonValue v;
        JsonValue::Set(v, JsonType::Object);
        pos_++;  // consume '{'
        SkipWhitespace();
        if (Consume('}')) return v;
        while (true) {
            SkipWhitespace();
            if (Peek() != '"') {
                Fail("expected string key in object");
                return v;
            }
            JsonValue key = ParseString();
            SkipWhitespace();
            if (!Consume(':')) {
                Fail("expected ':' after object key");
                return v;
            }
            JsonValue value = ParseValue();
            if (failed_) return v;
            v.object_.emplace_back(key.AsString(), std::move(value));
            SkipWhitespace();
            if (Consume(',')) continue;
            if (Consume('}')) break;
            Fail("expected ',' or '}' in object");
            return v;
        }
        return v;
    }

    JsonValue ParseArray() {
        JsonValue v;
        JsonValue::Set(v, JsonType::Array);
        pos_++;  // consume '['
        SkipWhitespace();
        if (Consume(']')) return v;
        while (true) {
            JsonValue value = ParseValue();
            if (failed_) return v;
            v.array_.push_back(std::move(value));
            SkipWhitespace();
            if (Consume(',')) continue;
            if (Consume(']')) break;
            Fail("expected ',' or ']' in array");
            return v;
        }
        return v;
    }

    JsonValue ParseString() {
        JsonValue v;
        JsonValue::Set(v, JsonType::String);
        pos_++;  // consume opening quote
        std::string out;
        while (true) {
            if (pos_ >= text_.size()) {
                Fail("unterminated string");
                return v;
            }
            char c = text_[pos_++];
            if (c == '"') break;
            if (c == '\\') {
                if (pos_ >= text_.size()) {
                    Fail("unterminated escape sequence");
                    return v;
                }
                char esc = text_[pos_++];
                switch (esc) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u':
                        // Effect package text fields (names, categories,
                        // ids) are ASCII in every package this engine
                        // ships or generates; a full UTF-16 surrogate-pair
                        // decode isn't worth the complexity here. Emit a
                        // placeholder rather than misparsing byte offsets.
                        pos_ += 4;
                        out += '?';
                        break;
                    default:
                        Fail("invalid escape sequence");
                        return v;
                }
            } else {
                out += c;
            }
        }
        v.string_ = std::move(out);
        return v;
    }

    JsonValue ParseBool() {
        JsonValue v;
        JsonValue::Set(v, JsonType::Bool);
        if (ConsumeLiteral("true")) {
            v.bool_ = true;
        } else if (ConsumeLiteral("false")) {
            v.bool_ = false;
        } else {
            Fail("invalid literal (expected true/false)");
        }
        return v;
    }

    JsonValue ParseNull() {
        JsonValue v;
        if (!ConsumeLiteral("null")) Fail("invalid literal (expected null)");
        return v;
    }

    JsonValue ParseNumber() {
        JsonValue v;
        JsonValue::Set(v, JsonType::Number);
        size_t start = pos_;
        if (Peek() == '-') pos_++;
        if (!std::isdigit(static_cast<unsigned char>(Peek()))) {
            Fail("invalid number");
            return v;
        }
        while (std::isdigit(static_cast<unsigned char>(Peek()))) pos_++;
        if (Peek() == '.') {
            pos_++;
            while (std::isdigit(static_cast<unsigned char>(Peek()))) pos_++;
        }
        if (Peek() == 'e' || Peek() == 'E') {
            pos_++;
            if (Peek() == '+' || Peek() == '-') pos_++;
            while (std::isdigit(static_cast<unsigned char>(Peek()))) pos_++;
        }
        v.number_ = std::strtod(text_.substr(start, pos_ - start).c_str(), nullptr);
        return v;
    }

    const std::string& text_;
    size_t pos_ = 0;
    std::string* error_;
    bool failed_ = false;
};

JsonValue JsonValue::Parse(const std::string& text, std::string* error) {
    JsonParser parser(text, error);
    return parser.ParseDocument();
}

}  // namespace nle
