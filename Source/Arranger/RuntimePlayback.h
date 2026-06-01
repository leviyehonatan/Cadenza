#pragma once

#include "PatternTransposer.h"
#include "Style.h"

#include <optional>
#include <string>
#include <vector>

namespace cadenza::audio
{
class Transport;
}

namespace cadenza::arranger
{
struct PartPlaybackSetup
{
    std::string partName;
    int sourceChannel = 0;
    int cadenzaChannel = 0;  // playback channel, using 1-based Cadenza channel numbers.
    std::optional<int> synthChannel;
    std::optional<int> bankMsb;
    std::optional<int> bankLsb;
    std::optional<int> program;
    std::optional<int> volume;
    std::optional<int> pan;
    std::optional<int> reverb;
    std::optional<int> chorus;
    bool percussion = false;
    int noteCount = 0;
};

int playbackChannelForPart(const Part& part) noexcept;
PartPlaybackSetup playbackSetupForPart(const Part& part);
std::vector<PartPlaybackSetup> playbackSetupsForSection(const Section& section);
void applyStyleTimingToTransport(cadenza::audio::Transport& transport,
                                 const Style& style,
                                 bool applyTempo = true);

struct DrumNoteRemap
{
    int originalNote = 0;
    int playbackNote = 0;
    bool yamahaXg = false;
    bool remapped = false;
};

bool isYamahaXgDrumPart(const Part& part);
int remapYamahaXgToGmDrumNote(int note) noexcept;
DrumNoteRemap drumNoteForPlayback(const Part& part, int note);
std::optional<int> playbackNoteForPart(const Part& part,
                                       const PatternNote& note,
                                       const TransposeContext& context);

// Build the TransposeContext used for STYLE (accompaniment) playback. Style
// parts follow the global transpose but NOT the Octave control — Octave is a
// live-keyboard / right-hand feature only — so globalOctave is forced to 0 here.
// This is the single seam that keeps Octave out of accompaniment playback.
TransposeContext makeStylePlaybackContext(const cadenza::midi::Chord& chord,
                                          int keyTonicPc,
                                          int transposeSemitones) noexcept;

// Apply the live-keyboard Octave control to a right-hand melody note, clamped to
// the valid MIDI range. Used for live melody input only, never for style parts.
int liveMelodyNote(int note, int octaves) noexcept;
}
