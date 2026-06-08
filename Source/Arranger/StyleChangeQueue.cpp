#include "StyleChangeQueue.h"

#include <utility>

namespace cadenza::arranger
{
void StyleChangeQueue::publish(std::shared_ptr<const Style> style)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending = std::move(style);
}

std::optional<std::shared_ptr<const Style>> StyleChangeQueue::tryTake()
{
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    if (!lock.owns_lock() || !m_pending)
        return std::nullopt;

    auto request = std::move(m_pending);
    m_pending.reset();
    return request;
}

void StyleChangeQueue::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending.reset();
}
}
