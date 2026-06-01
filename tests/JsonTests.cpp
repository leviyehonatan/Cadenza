#include "Json/Json.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
int failures = 0;

void expect(bool cond, const std::string& msg) {
    if (cond) return;
    ++failures;
    std::cerr << "FAIL: " << msg << '\n';
}

void parseAndSerializePrimitives()
{
    using namespace cadenza::json;

    expect(parse("null").isNull(), "parse null");
    expect(parse("true").asBool() == true, "parse true");
    expect(parse("false").asBool() == false, "parse false");
    expect(parse("42").asInt() == 42, "parse positive int");
    expect(parse("-17").asInt() == -17, "parse negative int");
    expect(parse("3.14").asNumber() > 3.13 && parse("3.14").asNumber() < 3.15, "parse double");
    expect(parse(R"("hello")").asString() == "hello", "parse string");
    expect(parse(R"("a\nb")").asString() == "a\nb", "parse escape \\n");
    expect(parse(R"("aAb")").asString() == "aAb", "parse \\u escape");

    expect(serialize(Value::null()) == "null", "serialize null");
    expect(serialize(Value::boolean(true)) == "true", "serialize true");
    expect(serialize(Value::number(7)) == "7", "serialize integer-like number");
    expect(serialize(Value::number(7.5)).find("7.5") != std::string::npos, "serialize fractional");
    expect(serialize(Value::string("a\nb")).find("\\n") != std::string::npos, "serialize escapes \\n");
}

void parseObjectsAndArrays()
{
    using namespace cadenza::json;

    auto v = parse(R"({"a":1,"b":[2,3,4],"c":{"d":"e"}})");
    expect(v.isObject(), "top is object");
    expect(v.get("a").asInt() == 1, "object a=1");
    expect(v.get("b").isArray(), "b is array");
    expect(v.get("b").asArray().size() == 3, "b has 3 elements");
    expect(v.get("b").asArray()[1].asInt() == 3, "b[1]=3");
    expect(v.get("c").get("d").asString() == "e", "nested c.d=e");
}

void roundTrip()
{
    using namespace cadenza::json;

    const std::string original = R"({"name":"test","count":42,"flags":[true,false,null],"nested":{"x":1.5}})";
    auto v = parse(original);
    expect(!v.isNull(), "round-trip parses");

    auto s = serialize(v, false);
    auto v2 = parse(s);
    expect(!v2.isNull(), "round-trip reparses");
    expect(v2.get("name").asString() == "test", "name preserved");
    expect(v2.get("count").asInt() == 42, "count preserved");
    expect(v2.get("flags").asArray().size() == 3, "array preserved");
    expect(v2.get("nested").get("x").asNumber() > 1.49 && v2.get("nested").get("x").asNumber() < 1.51,
           "nested fractional preserved");
}

void errorReporting()
{
    using namespace cadenza::json;

    ParseError err;
    auto v = parse("{not json}", &err);
    expect(v.isNull(), "malformed returns null");
    expect(!err.ok(), "error reported");

    auto v2 = parse(R"({"a":1,)", &err);
    expect(v2.isNull(), "incomplete object returns null");
    expect(!err.ok(), "incomplete error reported");
}

void prettyPrintAddsWhitespace()
{
    using namespace cadenza::json;
    Object o;
    o["a"] = Value::number(1);
    auto pretty = serialize(Value::object(std::move(o)), true);
    expect(pretty.find('\n') != std::string::npos, "pretty has newlines");
}
}

int main()
{
    parseAndSerializePrimitives();
    parseObjectsAndArrays();
    roundTrip();
    errorReporting();
    prettyPrintAddsWhitespace();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All JSON tests passed\n";
    return EXIT_SUCCESS;
}
