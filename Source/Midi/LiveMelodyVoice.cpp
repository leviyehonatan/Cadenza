#include "LiveMelodyVoice.h"

namespace cadenza::midi
{
LiveMelodyVoice::LiveMelodyVoice(int melodyChannel) noexcept
    : m_channel(melodyChannel)
{
    m_playedNote.fill(-1);
}

int LiveMelodyVoice::clampMidi(int n) noexcept
{
    if (n < 0)   return 0;
    if (n > 127) return 127;
    return n;
}

std::optional<LiveMelodyEvent> LiveMelodyVoice::handleNote(int note, int velocity,
                                                           bool isOn, bool isMelodyZone) noexcept
{
    if (note < 0 || note > 127)
        return std::nullopt;

    if (isOn) {
        if (!isMelodyZone)
            return std::nullopt;                 // chord-zone / ignored: no live melody sound

        const int played = clampMidi(note + 12 * m_octave.load() + m_transpose.load());
        m_playedNote[static_cast<std::size_t>(note)] = played;
        return LiveMelodyEvent{ m_channel, played, velocity, true };
    }

    // Note-off: release the exact pitch we sounded, even if Octave changed since.
    const int played = m_playedNote[static_cast<std::size_t>(note)];
    if (played < 0)
        return std::nullopt;                     // never sounded as a melody note
    m_playedNote[static_cast<std::size_t>(note)] = -1;
    return LiveMelodyEvent{ m_channel, played, 0, false };
}

void LiveMelodyVoice::reset() noexcept
{
    m_playedNote.fill(-1);
}

int gmProgramForBankName(const std::string& name) noexcept
{
    // Maps the 16 UI "Bank Memory" voice names to reasonable General MIDI programs.
    struct Entry { const char* name; int program; };
    static const Entry table[] = {
        { "Piano",      0  },  // Acoustic Grand Piano
        { "El Grand",   2  },  // Electric Grand Piano
        { "Rhodes",     4  },  // Electric Piano 1
        { "FM Piano",   5  },  // Electric Piano 2
        { "Digi Piano", 1  },  // Bright Acoustic Piano
        { "Rock Piano", 3  },  // Honky-tonk Piano
        { "N. Guitar",  24 },  // Acoustic Nylon Guitar
        { "C. Guitar",  27 },  // Electric Clean Guitar
        { "Dist Solo",  30 },  // Distortion Guitar
        { "80's Lead",  81 },  // Sawtooth Lead
        { "Organ",      16 },  // Drawbar Organ
        { "Alto Sax",   65 },  // Alto Sax
        { "Tenor Sax",  66 },  // Tenor Sax
        { "Trumpet",    56 },  // Trumpet
        { "Power Pad",  88 },  // New Age Pad
        { "Synth Stab", 62 },  // Synth Brass 1
    };
    for (const auto& e : table)
        if (name == e.name)
            return e.program;
    return 0;  // fallback: Acoustic Grand Piano
}
}
