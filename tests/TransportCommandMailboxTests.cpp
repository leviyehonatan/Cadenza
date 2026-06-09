#include "Audio/TransportCommandMailbox.h"

#include <array>
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

using cadenza::audio::TransportCommand;
using cadenza::audio::TransportCommandMailbox;
using cadenza::audio::TransportCommandType;

void commandsAreConsumedInOrder()
{
    TransportCommandMailbox mailbox;
    mailbox.publish({ TransportCommandType::Play, 0.0 });
    mailbox.publish({ TransportCommandType::SetBpm, 137.5 });
    mailbox.publish({ TransportCommandType::Stop, 0.0 });

    std::array<TransportCommand, TransportCommandMailbox::capacity> commands {};
    const auto count = mailbox.tryTakeAll(commands);

    expect(count == 3, "all published commands are consumed");
    expect(commands[0].type == TransportCommandType::Play, "play remains first");
    expect(commands[1].type == TransportCommandType::SetBpm, "tempo remains second");
    expect(commands[1].bpm == 137.5, "tempo payload is preserved");
    expect(commands[2].type == TransportCommandType::Stop, "stop remains third");
    expect(mailbox.tryTakeAll(commands) == 0, "commands are consumed exactly once");
}

void mailboxUsesFixedCapacity()
{
    TransportCommandMailbox mailbox;
    bool allAccepted = true;
    for (std::size_t i = 0; i < TransportCommandMailbox::capacity; ++i)
        allAccepted = mailbox.publish({ TransportCommandType::SetBpm, 80.0 + i }) && allAccepted;

    expect(allAccepted, "commands up to fixed capacity are accepted");
    expect(!mailbox.publish({ TransportCommandType::Stop, 0.0 }),
           "overflow reports that the oldest command was replaced");

    std::array<TransportCommand, TransportCommandMailbox::capacity> commands {};
    const auto count = mailbox.tryTakeAll(commands);
    expect(count == TransportCommandMailbox::capacity, "mailbox remains bounded");
    expect(commands[0].bpm == 81.0, "overflow discards the oldest command");
    expect(commands[count - 1].type == TransportCommandType::Stop,
           "newest command is retained on overflow");
}
}

int main()
{
    commandsAreConsumedInOrder();
    mailboxUsesFixedCapacity();

    if (failures != 0)
        return EXIT_FAILURE;

    std::cout << "All TransportCommandMailbox tests passed\n";
    return EXIT_SUCCESS;
}
