#include "Json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <stdexcept>

namespace cadenza::json
{
const std::string Value::s_emptyString{};
const Value       Value::s_nullValue{};
const Array       Value::s_emptyArray{};
const Object      Value::s_emptyObject{};

Value Value::null()                      { Value v; v.m_type = Type::Null;    return v; }
Value Value::boolean(bool b)             { Value v; v.m_type = Type::Boolean; v.m_bool = b; return v; }
Value Value::number(double n)            { Value v; v.m_type = Type::Number;  v.m_number = n; return v; }
Value Value::string(std::string s)       { Value v; v.m_type = Type::String;  v.m_string = std::move(s); return v; }
Value Value::array(Array a)              { Value v; v.m_type = Type::Array;   v.m_array = std::make_shared<Array>(std::move(a)); return v; }
Value Value::object(Object o)            { Value v; v.m_type = Type::Object;  v.m_object = std::make_shared<Object>(std::move(o)); return v; }

bool   Value::asBool(bool fallback)        const noexcept { return m_type == Type::Boolean ? m_bool : fallback; }
double Value::asNumber(double fallback)    const noexcept { return m_type == Type::Number  ? m_number : fallback; }
int    Value::asInt(int fallback)          const noexcept { return m_type == Type::Number  ? static_cast<int>(m_number) : fallback; }

const std::string& Value::asString(const std::string& fallback) const noexcept
{
    return m_type == Type::String ? m_string : fallback;
}

const Array&  Value::asArray()  const noexcept { return (m_type == Type::Array  && m_array)  ? *m_array  : s_emptyArray; }
const Object& Value::asObject() const noexcept { return (m_type == Type::Object && m_object) ? *m_object : s_emptyObject; }

Array& Value::mutableArray()
{
    if (m_type != Type::Array) { m_type = Type::Array; m_array = std::make_shared<Array>(); }
    if (!m_array) m_array = std::make_shared<Array>();
    return *m_array;
}

Object& Value::mutableObject()
{
    if (m_type != Type::Object) { m_type = Type::Object; m_object = std::make_shared<Object>(); }
    if (!m_object) m_object = std::make_shared<Object>();
    return *m_object;
}

const Value& Value::get(const std::string& key) const noexcept
{
    if (m_type != Type::Object || !m_object) return s_nullValue;
    auto it = m_object->find(key);
    return it == m_object->end() ? s_nullValue : it->second;
}

bool Value::has(const std::string& key) const noexcept
{
    if (m_type != Type::Object || !m_object) return false;
    return m_object->find(key) != m_object->end();
}

// ============================================================
// PARSER
// ============================================================
namespace
{
struct Parser
{
    const std::string& src;
    std::size_t pos = 0;
    bool ok = true;
    std::string errorMessage;
    std::size_t errorOffset = 0;

    explicit Parser(const std::string& s) : src(s) {}

    void fail(const char* msg) {
        if (!ok) return;
        ok = false;
        errorMessage = msg;
        errorOffset = pos;
    }

    void skipWs() {
        while (pos < src.size()) {
            char c = src[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos;
            else break;
        }
    }

    bool consume(char c) {
        skipWs();
        if (pos < src.size() && src[pos] == c) { ++pos; return true; }
        return false;
    }

    Value parseValue() {
        skipWs();
        if (!ok || pos >= src.size()) { fail("unexpected end of input"); return Value::null(); }

        char c = src[pos];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseString();
        if (c == 't' || c == 'f') return parseBoolean();
        if (c == 'n') return parseNull();
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();

        fail("unexpected character");
        return Value::null();
    }

    Value parseObject() {
        if (!consume('{')) { fail("expected '{'"); return Value::null(); }
        Object obj;
        skipWs();
        if (consume('}')) return Value::object(std::move(obj));

        while (ok) {
            skipWs();
            if (pos >= src.size() || src[pos] != '"') { fail("expected string key"); break; }
            auto key = readStringRaw();
            if (!ok) break;
            skipWs();
            if (!consume(':')) { fail("expected ':'"); break; }
            auto val = parseValue();
            if (!ok) break;
            obj.emplace(std::move(key), std::move(val));
            skipWs();
            if (consume(',')) continue;
            if (consume('}')) return Value::object(std::move(obj));
            fail("expected ',' or '}'"); break;
        }
        return Value::null();
    }

    Value parseArray() {
        if (!consume('[')) { fail("expected '['"); return Value::null(); }
        Array arr;
        skipWs();
        if (consume(']')) return Value::array(std::move(arr));

        while (ok) {
            arr.push_back(parseValue());
            if (!ok) break;
            skipWs();
            if (consume(',')) continue;
            if (consume(']')) return Value::array(std::move(arr));
            fail("expected ',' or ']'"); break;
        }
        return Value::null();
    }

    Value parseString() {
        return Value::string(readStringRaw());
    }

    std::string readStringRaw() {
        if (pos >= src.size() || src[pos] != '"') { fail("expected '\"'"); return {}; }
        ++pos;
        std::string out;
        while (pos < src.size()) {
            char c = src[pos++];
            if (c == '"') return out;
            if (c == '\\') {
                if (pos >= src.size()) { fail("dangling escape"); return out; }
                char esc = src[pos++];
                switch (esc) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        if (pos + 4 > src.size()) { fail("bad unicode escape"); return out; }
                        int code = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = src[pos++];
                            code <<= 4;
                            if (h >= '0' && h <= '9') code |= (h - '0');
                            else if (h >= 'a' && h <= 'f') code |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') code |= (h - 'A' + 10);
                            else { fail("bad hex digit"); return out; }
                        }
                        // Minimal UTF-8 encoding (no surrogate-pair handling — fine for our use)
                        if (code < 0x80) {
                            out += static_cast<char>(code);
                        } else if (code < 0x800) {
                            out += static_cast<char>(0xC0 | (code >> 6));
                            out += static_cast<char>(0x80 | (code & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (code >> 12));
                            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (code & 0x3F));
                        }
                        break;
                    }
                    default: fail("bad escape"); return out;
                }
            } else {
                out += c;
            }
        }
        fail("unterminated string");
        return out;
    }

    Value parseBoolean() {
        if (pos + 4 <= src.size() && src.compare(pos, 4, "true") == 0)  { pos += 4; return Value::boolean(true); }
        if (pos + 5 <= src.size() && src.compare(pos, 5, "false") == 0) { pos += 5; return Value::boolean(false); }
        fail("invalid boolean"); return Value::null();
    }

    Value parseNull() {
        if (pos + 4 <= src.size() && src.compare(pos, 4, "null") == 0) { pos += 4; return Value::null(); }
        fail("invalid null"); return Value::null();
    }

    Value parseNumber() {
        std::size_t start = pos;
        if (pos < src.size() && src[pos] == '-') ++pos;
        while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') ++pos;
        if (pos < src.size() && src[pos] == '.') {
            ++pos;
            while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') ++pos;
        }
        if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
            ++pos;
            if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) ++pos;
            while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') ++pos;
        }
        if (start == pos) { fail("invalid number"); return Value::null(); }
        try {
            double d = std::stod(src.substr(start, pos - start));
            return Value::number(d);
        } catch (...) {
            fail("invalid number"); return Value::null();
        }
    }
};
}

Value parse(const std::string& source, ParseError* out_error)
{
    Parser p(source);
    auto v = p.parseValue();
    p.skipWs();
    if (p.ok && p.pos < source.size()) p.fail("trailing characters after document");
    if (out_error) {
        out_error->message = p.errorMessage;
        out_error->offset = p.errorOffset;
    }
    if (!p.ok) return Value::null();
    return v;
}

// ============================================================
// SERIALIZER
// ============================================================
namespace
{
void writeIndent(std::ostringstream& out, int depth)
{
    for (int i = 0; i < depth; ++i) out << "  ";
}

void writeString(std::ostringstream& out, const std::string& s)
{
    out << '"';
    for (char c : s) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out << buf;
                } else {
                    out << c;
                }
        }
    }
    out << '"';
}

void writeNumber(std::ostringstream& out, double n)
{
    // Prefer integer formatting when possible (no trailing zeros, no decimal point).
    if (std::isfinite(n) && std::floor(n) == n && std::abs(n) < 1e15) {
        out << static_cast<long long>(n);
    } else {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.17g", n);
        out << buf;
    }
}

void writeValue(std::ostringstream& out, const Value& v, bool pretty, int depth)
{
    switch (v.type()) {
        case Value::Type::Null:    out << "null"; return;
        case Value::Type::Boolean: out << (v.asBool() ? "true" : "false"); return;
        case Value::Type::Number:  writeNumber(out, v.asNumber()); return;
        case Value::Type::String:  writeString(out, v.asString()); return;
        case Value::Type::Array: {
            const auto& a = v.asArray();
            if (a.empty()) { out << "[]"; return; }
            out << '[';
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (pretty) { out << '\n'; writeIndent(out, depth + 1); }
                writeValue(out, a[i], pretty, depth + 1);
                if (i + 1 != a.size()) out << ',';
            }
            if (pretty) { out << '\n'; writeIndent(out, depth); }
            out << ']';
            return;
        }
        case Value::Type::Object: {
            const auto& o = v.asObject();
            if (o.empty()) { out << "{}"; return; }
            out << '{';
            std::size_t i = 0;
            for (const auto& [k, val] : o) {
                if (pretty) { out << '\n'; writeIndent(out, depth + 1); }
                writeString(out, k);
                out << (pretty ? ": " : ":");
                writeValue(out, val, pretty, depth + 1);
                if (++i != o.size()) out << ',';
            }
            if (pretty) { out << '\n'; writeIndent(out, depth); }
            out << '}';
            return;
        }
    }
}
}

std::string serialize(const Value& value, bool pretty)
{
    std::ostringstream out;
    writeValue(out, value, pretty, 0);
    return out.str();
}
}
