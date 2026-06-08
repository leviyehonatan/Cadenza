#include "Arranger/SectionChangeQueue.h"

#include <cstdlib>
#include <iostream>
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

using cadenza::arranger::SectionChangeQueue;

void publishedRequestIsConsumedOnce()
{
    SectionChangeQueue queue;
    queue.publish("mainB", true, "mainA");

    auto request = queue.tryTake();
    expect(request.has_value(), "published request is available");
    expect(request && request->name == "mainB", "section name is preserved");
    expect(request && request->once, "one-shot flag is preserved");
    expect(request && request->returnTo == "mainA", "return section is preserved");
    expect(!queue.tryTake().has_value(), "request is consumed exactly once");
}

void latestRequestReplacesOlderPendingRequest()
{
    SectionChangeQueue queue;
    queue.publish("mainB", false, {});
    queue.publish("ending", true, {});

    auto request = queue.tryTake();
    expect(request && request->name == "ending", "latest pending section wins");
    expect(request && request->once, "latest one-shot flag wins");
}

void clearDropsPendingRequest()
{
    SectionChangeQueue queue;
    queue.publish("fillAA", true, "mainA");
    queue.clear();

    expect(!queue.tryTake().has_value(), "clear removes pending request");
}
}

int main()
{
    publishedRequestIsConsumedOnce();
    latestRequestReplacesOlderPendingRequest();
    clearDropsPendingRequest();

    if (failures != 0)
        return EXIT_FAILURE;

    std::cout << "All SectionChangeQueue tests passed\n";
    return EXIT_SUCCESS;
}
