#include "SectionChangeQueue.h"

#include <utility>

namespace cadenza::arranger
{
void SectionChangeQueue::publish(std::string name, bool once, std::string returnTo)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending = SectionChangeRequest { std::move(name), once, std::move(returnTo) };
}

std::optional<SectionChangeRequest> SectionChangeQueue::tryTake()
{
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    if (!lock.owns_lock() || !m_pending)
        return std::nullopt;

    auto request = std::move(m_pending);
    m_pending.reset();
    return request;
}

void SectionChangeQueue::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending.reset();
}
}
