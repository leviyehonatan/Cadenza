#include "TransportCommandMailbox.h"

namespace cadenza::audio
{
bool TransportCommandMailbox::publish(TransportCommand command)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const bool hadCapacity = m_size < capacity;
    if (!hadCapacity) {
        for (std::size_t i = 1; i < capacity; ++i)
            m_commands[i - 1] = m_commands[i];
        --m_size;
    }

    m_commands[m_size++] = command;
    return hadCapacity;
}

std::size_t TransportCommandMailbox::tryTakeAll(
    std::array<TransportCommand, capacity>& destination)
{
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    if (!lock.owns_lock())
        return 0;

    const auto count = m_size;
    for (std::size_t i = 0; i < count; ++i)
        destination[i] = m_commands[i];
    m_size = 0;
    return count;
}
}
