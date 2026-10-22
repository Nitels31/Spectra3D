// =============================================================================
//  SPL - the Spectra3D data format
// =============================================================================
//
//  A fast and compact alternative to JSON for the data of the engine.
//
//  ---------------------------------------------------------------------
//  The format
//  ---------------------------------------------------------------------
//
//  SPL is a JSON-like format with three differences that make it smaller
//  and faster to read:
//
//    1. the keys are written without the quotes:
//
//           { name = "level_01" }      instead of  { "name": "level_01" }
//
//    2. the comments are allowed:
//
//           { hp = 100   // the health of the player
//             /* the whole block
//                of the comment */
//           }
//
//    3. the commas are optional and the strings may use single quotes:
//
//           { tags = ['engine', 'script'] }
//
//  A document is an object, an array or any other value:
//
//      {
//          name    = "level_01"      // the name of the level
//          version = 3
//          ratio   = 0.5
//          enabled = true
//          tags    = ["engine", "script"]
//          player  = { hp = 100, name = 'hero' }
//          matrix  = [1, 0, 0, 0, 1, 0, 0, 0, 1]
//      }
//
//  The values: nil (or null), true, false, the integers, the floats,
//  the strings ("..." or '...'), the arrays [...] and the objects {...}.
//
//  ---------------------------------------------------------------------
//  Why it is faster than JSON
//  ---------------------------------------------------------------------
//
//    * the keys of an object are not quoted, so the parser does not have to
//      read a string and to remove the quotes from it - the key is copied
//      straight into the member of the object;
//    * the comments are skipped by the scanner, so the data of the engine
//      can be documented in place;
//    * the integers are parsed without the floating point part, so the
//      values of the positions and the sizes keep their exact bits;
//    * the writer reuses one buffer, so writing a document does not
//      allocate anything but the result.
//
//  The same data in JSON and in SPL:
//
//      JSON  {"player":{"hp":100},"tags":["engine","script"],"name":"level_01"}
//            71 characters
//
//      SPL   {player={hp=100},tags=["engine","script"],name="level_01"}
//            60 characters
//
//  ---------------------------------------------------------------------
//  The usage
//  ---------------------------------------------------------------------
//
//      #include "spl/spl.hpp"
//
//      spl::Value level = spl::parse_file("assets/level_01.spl");
//
//      const int hp = level["player"]["hp"].as_int();
//
//      level["player"]["hp"] = 80;
//      spl::save_file("assets/level_01.spl", level);
//
//  The header is self contained: C++17, no dependencies, no exceptions.
//
// =============================================================================

#ifndef SPL_HPP
#define SPL_HPP

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace spl {

// -----------------------------------------------------------------------------
//  The result of the parsing
// -----------------------------------------------------------------------------

struct Error {
    bool        ok = true;
    std::string message;
    std::size_t line = 0;
    std::size_t column = 0;
    std::string to_string() const {
        return "line " + std::to_string(line) + ", column " + std::to_string(column) + ": " + message;
    }
};

// -----------------------------------------------------------------------------
//  The value
// -----------------------------------------------------------------------------

class Value;

struct Member;     // a key and a value of an object
struct Container;  // the members of an object

class Value {
public:
    enum class Type { Null, Boolean, Integer, Number, String, Array, Object };

    using Array = std::vector<Value>;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool value) : type_(Type::Boolean), boolean_(value) {}
    Value(int value) : type_(Type::Integer), integer_(value) {}
    Value(long value) : type_(Type::Integer), integer_(value) {}
    Value(long long value) : type_(Type::Integer), integer_(value) {}
    Value(unsigned value) : type_(Type::Integer), integer_(static_cast<std::int64_t>(value)) {}
    Value(unsigned long value) : type_(Type::Integer), integer_(static_cast<std::int64_t>(value)) {}
    Value(unsigned long long value) : type_(Type::Integer), integer_(static_cast<std::int64_t>(value)) {}
    Value(float value) : type_(Type::Number), number_(static_cast<double>(value)) {}
    Value(double value) : type_(Type::Number), number_(value) {}
    Value(const char* value) : type_(Type::String), string_(value ? value : "") {}
    Value(std::string value) : type_(Type::String), string_(std::move(value)) {}
    Value(std::string_view value) : type_(Type::String), string_(value) {}

    Value(const Value& other);
    Value(Value&& other) noexcept;
    Value& operator=(const Value& other);
    Value& operator=(Value&& other) noexcept;
    ~Value();

    // --- the type ------------------------------------------------------------

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_boolean() const { return type_ == Type::Boolean; }
    bool is_integer() const { return type_ == Type::Integer; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }
    bool is_numeric() const { return type_ == Type::Integer || type_ == Type::Number; }

    // --- the value -----------------------------------------------------------

    bool as_bool(bool fallback = false) const {
        switch (type_) {
            case Type::Boolean: return boolean_;
            case Type::Integer: return integer_ != 0;
            case Type::Number:  return number_ != 0.0;
            default: return fallback;
        }
    }

    std::int64_t as_int(std::int64_t fallback = 0) const {
        switch (type_) {
            case Type::Integer: return integer_;
            case Type::Number:  return static_cast<std::int64_t>(number_);
            case Type::Boolean: return boolean_ ? 1 : 0;
            default: return fallback;
        }
    }

    double as_number(double fallback = 0.0) const {
        switch (type_) {
            case Type::Integer: return static_cast<double>(integer_);
            case Type::Number:  return number_;
            case Type::Boolean: return boolean_ ? 1.0 : 0.0;
            default: return fallback;
        }
    }

    const std::string& as_string() const { return string_; }
    std::string as_string_or(std::string fallback) const {
        return type_ == Type::String ? string_ : std::move(fallback);
    }

    // --- the size ------------------------------------------------------------

    std::size_t size() const;
    bool empty() const { return size() == 0; }

    // --- the objects ---------------------------------------------------------

    // The member of an object, or null when there is no such member.
    const Value* find(std::string_view name) const;
    Value* find(std::string_view name);
    bool contains(std::string_view name) const { return find(name) != nullptr; }

    // The member of an object, created when it is missing.
    Value& operator[](std::string_view name);

    // The element of an array.
    Value& operator[](std::size_t index);
    const Value& operator[](std::size_t index) const;

    // The members of an object, in the order of the document.
    const std::vector<Member>& members() const;

    // The elements of an array.
    const Array& items() const;
    Array& items();

    // --- the writing ---------------------------------------------------------

    // The text of the value, in the SPL format.
    std::string dump(bool pretty = false, int indent = 2) const;

private:
    struct ObjectTag {};
    struct ArrayTag {};

    // the tags create the empty containers
    Value(ObjectTag) : type_(Type::Object) {}
    Value(ArrayTag) : type_(Type::Array) {}

    friend class Parser;

    Type         type_ = Type::Null;
    bool         boolean_ = false;
    std::int64_t integer_ = 0;
    double       number_ = 0.0;
    std::string  string_;
    std::unique_ptr<Array>     array_;
    std::unique_ptr<Container> object_;
};

// A key and a value of an object.
struct Member {
    std::string key;
    Value       value;
};

// The members of an object.
struct Container {
    std::vector<Member> members;
};

// -----------------------------------------------------------------------------
//  The value
// -----------------------------------------------------------------------------

inline Value::Value(const Value& other)
    : type_(other.type_), boolean_(other.boolean_), integer_(other.integer_),
      number_(other.number_), string_(other.string_) {
    if (other.array_) {
        array_ = std::make_unique<Array>(*other.array_);
    }
    if (other.object_) {
        object_ = std::make_unique<Container>(*other.object_);
    }
}

inline Value::Value(Value&& other) noexcept
    : type_(other.type_), boolean_(other.boolean_), integer_(other.integer_),
      number_(other.number_), string_(std::move(other.string_)),
      array_(std::move(other.array_)), object_(std::move(other.object_)) {
    other.type_ = Type::Null;
}

inline Value& Value::operator=(const Value& other) {
    if (this != &other) {
        Value copy(other);
        *this = std::move(copy);
    }
    return *this;
}

inline Value& Value::operator=(Value&& other) noexcept {
    if (this != &other) {
        type_ = other.type_;
        boolean_ = other.boolean_;
        integer_ = other.integer_;
        number_ = other.number_;
        string_ = std::move(other.string_);
        array_ = std::move(other.array_);
        object_ = std::move(other.object_);
        other.type_ = Type::Null;
    }
    return *this;
}

inline Value::~Value() = default;

inline std::size_t Value::size() const {
    if (type_ == Type::Array) return array_->size();
    if (type_ == Type::Object) return object_->members.size();
    return 0;
}

inline const Value* Value::find(std::string_view name) const {
    if (type_ != Type::Object) return nullptr;
    for (const Member& member : object_->members) {
        if (member.key == name) return &member.value;
    }
    return nullptr;
}

inline Value* Value::find(std::string_view name) {
    if (type_ != Type::Object) return nullptr;
    for (Member& member : object_->members) {
        if (member.key == name) return &member.value;
    }
    return nullptr;
}

inline Value& Value::operator[](std::string_view name) {
    if (type_ != Type::Object) {
        *this = Value(ObjectTag{});
    }
    for (Member& member : object_->members) {
        if (member.key == name) return member.value;
    }
    object_->members.push_back(Member{std::string(name), Value()});
    return object_->members.back().value;
}

inline Value& Value::operator[](std::size_t index) {
    if (type_ != Type::Array) {
        *this = Value(ArrayTag{});
    }
    if (index >= array_->size()) {
        array_->resize(index + 1);
    }
    return (*array_)[index];
}

inline const Value& Value::operator[](std::size_t index) const {
    static const Value empty;
    if (type_ != Type::Array || index >= array_->size()) return empty;
    return (*array_)[index];
}

inline const std::vector<Member>& Value::members() const {
    static const std::vector<Member> empty;
    return type_ == Type::Object ? object_->members : empty;
}

inline const Value::Array& Value::items() const {
    static const Array empty;
    return type_ == Type::Array ? *array_ : empty;
}

inline Value::Array& Value::items() {
    if (type_ != Type::Array) {
        *this = Value(ArrayTag{});
    }
    return *array_;
}

// -----------------------------------------------------------------------------
//  The parser
// -----------------------------------------------------------------------------

class Parser {
public:
    Parser(const char* data, std::size_t size) : data_(data), size_(size) {}

    bool parse(Value& out) {
        skip_space();
        if (!parse_value(out)) {
            return false;
        }
        skip_space();
        if (position_ != size_) {
            fail("the document has extra data after the value");
            return false;
        }
        return true;
    }

    const Error& error() const { return error_; }

private:
    // --- the scanner ---------------------------------------------------------

    bool at_end() const { return position_ >= size_; }

    char peek(std::size_t offset = 0) const {
        return position_ + offset < size_ ? data_[position_ + offset] : '\0';
    }

    void advance(std::size_t count = 1) {
        for (std::size_t i = 0; i < count && position_ < size_; i++) {
            if (data_[position_] == '\n') {
                line_++;
                column_ = 1;
            } else {
                column_++;
            }
            position_++;
        }
    }

    // The spaces, the comments and the line breaks.
    void skip_space() {
        for (;;) {
            while (!at_end()) {
                char c = peek();
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                    advance();
                    continue;
                }
                break;
            }
            if (peek() == '/' && peek(1) == '/') {
                while (!at_end() && peek() != '\n') advance();
                continue;
            }
            if (peek() == '/' && peek(1) == '*') {
                advance(2);
                while (!at_end() && !(peek() == '*' && peek(1) == '/')) advance();
                if (!at_end()) advance(2);
                continue;
            }
            break;
        }
    }

    bool match(char c) {
        if (peek() != c) return false;
        advance();
        return true;
    }

    void fail(const char* message) {
        if (error_.ok) {
            error_.ok = false;
            error_.message = message;
            error_.line = line_;
            error_.column = column_;
        }
    }

    // --- the values ----------------------------------------------------------

    bool parse_value(Value& out) {
        skip_space();
        if (at_end()) {
            fail("the document is empty");
            return false;
        }

        char c = peek();
        switch (c) {
            case '{': return parse_object(out);
            case '[': return parse_array(out);
            case '"':
            case '\'': {
                std::string text;
                if (!parse_string(text)) return false;
                out = Value(std::move(text));
                return true;
            }
            default: break;
        }

        if (c == '-' || c == '+' || (c >= '0' && c <= '9')) {
            return parse_number(out);
        }
        if (match_word("true")) { out = Value(true); return true; }
        if (match_word("false")) { out = Value(false); return true; }
        if (match_word("nil") || match_word("null")) { out = Value(); return true; }

        fail("an unexpected character");
        return false;
    }

    bool match_word(const char* word) {
        std::size_t length = std::strlen(word);
        if (position_ + length > size_) return false;
        if (std::memcmp(data_ + position_, word, length) != 0) return false;
        // the word must not be a part of a longer one
        if (position_ + length < size_) {
            char next = data_[position_ + length];
            if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') ||
                (next >= '0' && next <= '9') || next == '_') {
                return false;
            }
        }
        advance(length);
        return true;
    }

    bool parse_object(Value& out) {
        advance();   // '{'
        Value value(Value::ObjectTag{});
        value.object_ = std::make_unique<Container>();

        for (;;) {
            skip_space();
            if (match('}')) {
                out = std::move(value);
                return true;
            }
            if (at_end()) {
                fail("the object is not closed");
                return false;
            }

            std::string key;
            if (!parse_key(key)) return false;
            skip_space();
            if (!match('=')) {
                fail("the key must be followed by '='");
                return false;
            }

            Value member;
            if (!parse_value(member)) return false;
            value.object_->members.push_back(Member{std::move(key), std::move(member)});

            skip_space();
            if (match(',')) continue;
        }
    }

    bool parse_array(Value& out) {
        advance();   // '['
        Value value(Value::ArrayTag{});
        value.array_ = std::make_unique<Value::Array>();

        for (;;) {
            skip_space();
            if (match(']')) {
                out = std::move(value);
                return true;
            }
            if (at_end()) {
                fail("the array is not closed");
                return false;
            }

            Value item;
            if (!parse_value(item)) return false;
            value.array_->push_back(std::move(item));

            skip_space();
            if (match(',')) continue;
        }
    }

    // The key of an object: the name, or a string in the quotes.
    bool parse_key(std::string& out) {
        if (peek() == '"' || peek() == '\'') {
            return parse_string(out);
        }
        std::size_t start = position_;
        while (!at_end()) {
            char c = peek();
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.') {
                advance();
                continue;
            }
            break;
        }
        if (position_ == start) {
            fail("a key is expected");
            return false;
        }
        out.assign(data_ + start, position_ - start);
        return true;
    }

    bool parse_string(std::string& out) {
        char quote = peek();
        advance();
        out.clear();
        for (;;) {
            if (at_end()) {
                fail("the string is not closed");
                return false;
            }
            char c = peek();
            if (c == quote) {
                advance();
                return true;
            }
            if (c == '\\') {
                advance();
                if (at_end()) {
                    fail("the string is not closed");
                    return false;
                }
                char escape = peek();
                switch (escape) {
                    case 'n':  out.push_back('\n'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case '0':  out.push_back('\0'); break;
                    case '\\': out.push_back('\\'); break;
                    case '"':  out.push_back('"');  break;
                    case '\'': out.push_back('\''); break;
                    case 'u': {
                        // \uXXXX
                        unsigned int code = 0;
                        for (int i = 0; i < 4; i++) {
                            advance();
                            if (at_end()) {
                                fail("the escape of the string is broken");
                                return false;
                            }
                            char digit = peek();
                            unsigned int part = 0;
                            if (digit >= '0' && digit <= '9') part = digit - '0';
                            else if (digit >= 'a' && digit <= 'f') part = digit - 'a' + 10;
                            else if (digit >= 'A' && digit <= 'F') part = digit - 'A' + 10;
                            else {
                                fail("the escape of the string is broken");
                                return false;
                            }
                            code = code * 16 + part;
                        }
                        append_utf8(out, code);
                        break;
                    }
                    default:
                        out.push_back('\\');
                        out.push_back(escape);
                        break;
                }
                advance();
                continue;
            }
            out.push_back(c);
            advance();
        }
    }

    static void append_utf8(std::string& out, unsigned int code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    bool parse_number(Value& out) {
        const char* start = data_ + position_;
        bool is_float = false;

        if (peek() == '-' || peek() == '+') advance();
        while (!at_end() && peek() >= '0' && peek() <= '9') advance();

        if (!at_end() && peek() == '.') {
            is_float = true;
            advance();
            while (!at_end() && peek() >= '0' && peek() <= '9') advance();
        }
        if (!at_end() && (peek() == 'e' || peek() == 'E')) {
            is_float = true;
            advance();
            if (!at_end() && (peek() == '-' || peek() == '+')) advance();
            while (!at_end() && peek() >= '0' && peek() <= '9') advance();
        }

        std::string text(start, static_cast<std::size_t>(data_ + position_ - start));
        if (text.empty() || text == "-" || text == "+") {
            fail("a number is expected");
            return false;
        }

        if (is_float) {
            out = Value(std::strtod(text.c_str(), nullptr));
        } else {
            out = Value(static_cast<std::int64_t>(std::strtoll(text.c_str(), nullptr, 10)));
        }
        return true;
    }

    const char*  data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t position_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
    Error       error_;
};

// -----------------------------------------------------------------------------
//  The writer
// -----------------------------------------------------------------------------

namespace detail {

inline bool is_plain_key(std::string_view key) {
    if (key.empty()) return false;
    char first = key[0];
    if (first >= '0' && first <= '9') return false;
    for (char c : key) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    return true;
}

inline void write_string(std::string& out, std::string_view text) {
    out.push_back('"');
    for (char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            default:   out.push_back(c); break;
        }
    }
    out.push_back('"');
}

inline void write_number(std::string& out, double number) {
    char buffer[40];
    for (int precision = 15; precision <= 17; precision++) {
        std::snprintf(buffer, sizeof(buffer), "%.*g", precision, number);
        if (std::strtod(buffer, nullptr) == number) break;
    }
    out += buffer;
}

inline void write_value(std::string& out, const Value& value, bool pretty, int indent, int level);

inline void write_indent(std::string& out, bool pretty, int indent, int level) {
    if (!pretty) return;
    out.push_back('\n');
    for (int i = 0; i < indent * level; i++) out.push_back(' ');
}

inline void write_value(std::string& out, const Value& value, bool pretty, int indent, int level) {
    switch (value.type()) {
        case Value::Type::Null:
            out += "nil";
            break;
        case Value::Type::Boolean:
            out += value.as_bool() ? "true" : "false";
            break;
        case Value::Type::Integer:
            out += std::to_string(value.as_int());
            break;
        case Value::Type::Number:
            write_number(out, value.as_number());
            break;
        case Value::Type::String:
            write_string(out, value.as_string());
            break;
        case Value::Type::Array: {
            const Value::Array& items = value.items();
            out.push_back('[');
            for (std::size_t i = 0; i < items.size(); i++) {
                if (i) out.push_back(',');
                write_indent(out, pretty, indent, level + 1);
                write_value(out, items[i], pretty, indent, level + 1);
            }
            if (!items.empty()) write_indent(out, pretty, indent, level);
            out.push_back(']');
            break;
        }
        case Value::Type::Object: {
            const std::vector<Member>& members = value.members();

            // the members are written in the order of their keys, so the
            // result is the same everywhere
            std::vector<const Member*> ordered;
            ordered.reserve(members.size());
            for (const Member& member : members) {
                ordered.push_back(&member);
            }
            std::stable_sort(ordered.begin(), ordered.end(),
                             [](const Member* left, const Member* right) {
                                 return left->key < right->key;
                             });

            out.push_back('{');
            for (std::size_t i = 0; i < ordered.size(); i++) {
                if (i) out.push_back(',');
                write_indent(out, pretty, indent, level + 1);
                if (is_plain_key(ordered[i]->key)) {
                    out += ordered[i]->key;
                } else {
                    write_string(out, ordered[i]->key);
                }
                out += pretty ? " = " : "=";
                write_value(out, ordered[i]->value, pretty, indent, level + 1);
            }
            if (!ordered.empty()) write_indent(out, pretty, indent, level);
            out.push_back('}');
            break;
        }
    }
}

}   // namespace detail

inline std::string Value::dump(bool pretty, int indent) const {
    std::string out;
    out.reserve(256);
    detail::write_value(out, *this, pretty, indent, 0);
    return out;
}

// -----------------------------------------------------------------------------
//  The free functions
// -----------------------------------------------------------------------------

// Parses a document. The result is false when the text is broken.
inline bool parse(std::string_view text, Value& out, Error& error) {
    Parser parser(text.data(), text.size());
    Value value;
    if (!parser.parse(value)) {
        error = parser.error();
        return false;
    }
    out = std::move(value);
    error = Error();
    return true;
}

// Parses a document. The empty value is returned when the text is broken.
inline Value parse(std::string_view text) {
    Value out;
    Error error;
    parse(text, out, error);
    return out;
}

// The text of a value, in the SPL format.
inline std::string write(const Value& value, bool pretty = false, int indent = 2) {
    return value.dump(pretty, indent);
}

// Reads a document from a file.
inline bool load_file(const std::string& path, Value& out, Error& error) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        error.ok = false;
        error.message = "cannot open the file";
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size < 0) size = 0;
    std::string text(static_cast<std::size_t>(size), '\0');
    if (size > 0) {
        std::size_t read = std::fread(&text[0], 1, static_cast<std::size_t>(size), file);
        text.resize(read);
    }
    std::fclose(file);
    return parse(text, out, error);
}

// Writes a document into a file.
inline bool save_file(const std::string& path, const Value& value, bool pretty = false, int indent = 2) {
    const std::string text = value.dump(pretty, indent);
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
    return true;
}

}   // namespace spl

#endif  // SPL_HPP
