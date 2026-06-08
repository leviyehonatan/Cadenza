#include "Arranger/StyleChangeQueue.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace
{
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

using cadenza::arranger::Style;
using cadenza::arranger::StyleChangeQueue;

std::shared_ptr<const Style> styleNamed(const std::string& name)
{
    auto style = std::make_shared<Style>();
    style->name = name;
    return style;
}

void publishedStyleIsConsumedOnce()
{
    StyleChangeQueue queue;
    queue.publish(styleNamed("First"));

    auto request = queue.tryTake();
    expect(request.has_value(), "published style is available");
    expect(request && *request && (*request)->name == "First", "style pointer is preserved");
    expect(!queue.tryTake().has_value(), "style request is consumed exactly once");
}

void latestStyleReplacesOlderPendingStyle()
{
    StyleChangeQueue queue;
    queue.publish(styleNamed("First"));
    queue.publish(styleNamed("Second"));

    auto request = queue.tryTake();
    expect(request && *request && (*request)->name == "Second", "latest pending style wins");
}

void nullStyleIsAReplacementRequest()
{
    StyleChangeQueue queue;
    queue.publish(nullptr);

    auto request = queue.tryTake();
    expect(request.has_value(), "null style remains a pending replacement");
    expect(request && !*request, "null style value is preserved");
}
}

int main()
{
    publishedStyleIsConsumedOnce();
    latestStyleReplacesOlderPendingStyle();
    nullStyleIsAReplacementRequest();

    if (failures != 0)
        return EXIT_FAILURE;

    std::cout << "All StyleChangeQueue tests passed\n";
    return EXIT_SUCCESS;
}
