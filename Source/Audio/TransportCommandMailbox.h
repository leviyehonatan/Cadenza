#pragma once

#include <array>
#include <cstddef>
#include <mutex>

namespace cadenza::audio
{
enum class TransportCommandType
{
    Play,
    Stop,
    SetBpm
};

struct TransportCommand
{
    TransportCommandType type = TransportCommandType::Stop;
    double bpm = 0.0;
};

class TransportCommandMailbox
{
public:
    static constexpr std::size_t capacity = 32;

    // Returns false when full and the oldest command had to be replaced.
    bool publish(TransportCommand command);

    // Audio-thread consumer. Never blocks; returns zero while a producer owns the lock.
    std::size_t tryTakeAll(std::array<TransportCommand, capacity>& destination);

private:
    std::mutex m_mutex;
    std::array<TransportCommand, capacity> m_commands {};
    std::size_t m_size = 0;
};
}
