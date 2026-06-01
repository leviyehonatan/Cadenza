// Cadenza minimal JSON parser/serializer.
// Hand-rolled, header + .cpp, no external dependency.
// Handles object/array/string/number/bool/null. No comments, no trailing commas.
// Good enough for Cadenza's own file formats; not a full RFC 8259 implementation.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cadenza::json
{
class Value;

using Object = std::unordered_map<std::string, Value>;
using Array  = std::vector<Value>;

class Value
{
public:
    enum class Type { Null, Boolean, Number, String, Array, Object };

    Value() = default;
    static Value null();
    static Value boolean(bool b);
    static Value number(double n);
    static Value string(std::string s);
    static Value array(Array a);
    static Value object(Object o);

    Type type() const noexcept { return m_type; }
    bool isNull()    const noexcept { return m_type == Type::Null; }
    bool isBool()    const noexcept { return m_type == Type::Boolean; }
    bool isNumber()  const noexcept { return m_type == Type::Number; }
    bool isString()  const noexcept { return m_type == Type::String; }
    bool isArray()   const noexcept { return m_type == Type::Array; }
    bool isObject()  const noexcept { return m_type == Type::Object; }

    bool        asBool(bool fallback = false) const noexcept;
    double      asNumber(double fallback = 0.0) const noexcept;
    int         asInt(int fallback = 0) const noexcept;
    const std::string& asString(const std::string& fallback = s_emptyString) const noexcept;
    const Array&  asArray()  const noexcept;
    const Object& asObject() const noexcept;

    // Object convenience: get a field with a fallback type-aware getter.
    const Value& get(const std::string& key) const noexcept;     // returns Null if missing
    bool         has(const std::string& key) const noexcept;

    // Mutation helpers (used by the parser).
    Array&  mutableArray();
    Object& mutableObject();

private:
    Type m_type = Type::Null;
    bool m_bool = false;
    double m_number = 0.0;
    std::string m_string;
    std::shared_ptr<Array>  m_array;
    std::shared_ptr<Object> m_object;

    static const std::string s_emptyString;
    static const Value       s_nullValue;
    static const Array       s_emptyArray;
    static const Object      s_emptyObject;
};

struct ParseError
{
    std::string message;
    std::size_t offset = 0;  // byte offset in input where error occurred
    bool ok() const noexcept { return message.empty(); }
};

// Parse a JSON document. On error, returned Value is Null and `out_error` (if non-null)
// is populated. Use ok() / message / offset to inspect.
Value parse(const std::string& source, ParseError* out_error = nullptr);

// Serialize a Value to a JSON string. If `pretty` is true, output is human-readable
// with 2-space indents.
std::string serialize(const Value& value, bool pretty = false);
}
