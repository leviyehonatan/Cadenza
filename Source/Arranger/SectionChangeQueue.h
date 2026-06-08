#pragma once

#include <mutex>
#include <optional>
#include <string>

namespace cadenza::arranger
{
struct SectionChangeRequest
{
    std::string name;
    bool once = false;
    std::string returnTo;
};

class SectionChangeQueue
{
public:
    void publish(std::string name, bool once, std::string returnTo);
    std::optional<SectionChangeRequest> tryTake();
    void clear();

private:
    std::mutex m_mutex;
    std::optional<SectionChangeRequest> m_pending;
};
}
