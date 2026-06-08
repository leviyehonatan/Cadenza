#include "LiveMelodyVoice.h"

#include <cctype>

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
    // Matching is intentionally forgiving: case-insensitive and punctuation-free,
    // so the native UI can evolve without breaking persisted settings or bridge
    // payloads that use longer descriptive names.
    auto normalize = [](const std::string& text) {
        std::string out;
        out.reserve(text.size());
        for (unsigned char c : text) {
            if (std::isalnum(c))
                out.push_back(static_cast<char>(std::tolower(c)));
        }
        return out;
    };

    const std::string key = normalize(name);

    struct Entry { const char* key; int program; };
    static const Entry table[] = {
        { "piano",      0  },  // Acoustic Grand Piano
        { "acousticgrandpiano", 0 },
        { "elgrand",   2  },  // Electric Grand Piano
        { "electricgrandpiano", 2 },
        { "rhodes",     4  },  // Electric Piano 1
        { "electricpiano1", 4 },
        { "fmpiano",    5  },  // Electric Piano 2
        { "electricpiano2", 5 },
        { "digipiano",  1  },  // Bright Acoustic Piano
        { "brightacousticpiano", 1 },
        { "rockpiano",  3  },  // Honky-tonk Piano
        { "honkytonkpiano", 3 },
        { "nguitar",    24 },  // Acoustic Nylon Guitar
        { "acousticnylonguitar", 24 },
        { "cguitar",    27 },  // Electric Clean Guitar
        { "electriccleanguitar", 27 },
        { "distsolo",   30 },  // Distortion Guitar
        { "distortionguitar", 30 },
        { "80slead",    81 },  // Sawtooth Lead
        { "sawlead",    81 },
        { "organ",      16 },  // Drawbar Organ
        { "drawbarorgan", 16 },
        { "altosax",    65 },  // Alto Sax
        { "tenorsax",   66 },  // Tenor Sax
        { "trumpet",    56 },  // Trumpet
        { "powerpad",   88 },  // New Age Pad
        { "newagepad",  88 },
        { "synthstab",  62 },  // Synth Brass 1
        { "synthbrass1", 62 },
    };
    for (const auto& e : table)
        if (key == e.key)
            return e.program;
    return 0;  // fallback: Acoustic Grand Piano
}
}
