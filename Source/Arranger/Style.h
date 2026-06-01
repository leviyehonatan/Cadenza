// Style data model — the in-memory representation of a `.cstyle` file.
// A style is a set of repeatable patterns ("sections") played by an arranger
// in response to live chord input. Each note in a pattern has a "role" that
// tells the transposer how to remap it when the chord changes.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cadenza::arranger
{
enum class YamahaStyleFormat
{
    Unknown,
    SFF1,
    SFF2,
};

enum class YamahaNtr
{
    RootTransposition,
    RootFixed,
    Guitar,
    Unknown,
};

enum class YamahaNtt
{
    Bypass,
    Melody,
    Chord,
    MelodicMinor,
    HarmonicMinor,
    NaturalMinor,
    Dorian,
    AllPurpose,
    Stroke,
    Arpeggio,
    Unknown,
};

enum class YamahaRetriggerRule
{
    Stop,
    PitchShift,
    PitchShiftToRoot,
    Retrigger,
    RetriggerToRoot,
    NoteGenerator,
    Unknown,
};

enum class YamahaPolicySource
{
    CASM,
    Ctb2,
    Ctab,
    Fallback,
};

struct YamahaChannelPolicy
{
    int sourceChannel = 0;                  // 1..16 when known.
    std::string destinationPart;
    std::string destinationType;
    std::string destinationName;
    std::optional<std::string> sourceRoot;
    std::optional<std::string> sourceChord;
    YamahaNtr ntr = YamahaNtr::Unknown;
    YamahaNtt ntt = YamahaNtt::Unknown;
    bool bassOn = false;
    std::optional<int> chordRootUpperLimit;
    std::optional<int> noteLowLimit;
    std::optional<int> noteHighLimit;
    YamahaRetriggerRule retriggerRule = YamahaRetriggerRule::Unknown;
    std::optional<uint8_t> rawNtr;
    std::optional<uint8_t> rawNtt;
    std::optional<uint8_t> rawRtr;
    YamahaPolicySource source = YamahaPolicySource::Fallback;
    std::vector<uint8_t> rawBytes;
};

enum class NoteRole
{
    Absolute,     // play the note as-is (drums, sound effects)
    ChordRoot,    // current chord root
    Chord3,       // current chord's 3rd
    Chord5,       // current chord's 5th
    Chord7,       // current chord's 7th
    ChordColor,   // a non-chord source tone (6th/9th/passing); follows the chord
                  // by root transposition, preserving the recorded voicing/interval
    ScaleTone,    // a scale degree in the current key (uses degree field)
};

struct PatternNote
{
    int tick = 0;          // start tick within the pattern (PPQ units)
    int duration = 0;      // in ticks
    int pitch = 60;        // base MIDI pitch (for Absolute) OR seed octave (for chord roles)
    int velocity = 100;    // 1..127
    NoteRole role = NoteRole::Absolute;
    int scaleDegree = 0;   // only used when role==ScaleTone (0..6, 0=tonic)
};

struct Part
{
    std::string name;           // "drums", "bass", "harmony"
    int midiChannel = 1;        // 1..16
    std::string instrument;     // soundfont preset name or VST plugin id
    std::optional<int> bankMsb;  // MIDI CC 0, 0..127 when known
    std::optional<int> bankLsb;  // MIDI CC 32, 0..127 when known
    std::optional<int> program;  // MIDI program, 0..127 when known
    std::optional<int> volume;   // MIDI CC 7, 0..127 when known
    std::optional<int> pan;      // MIDI CC 10, 0..127 when known
    std::optional<int> reverb;   // MIDI CC 91, 0..127 when known
    std::optional<int> chorus;   // MIDI CC 93, 0..127 when known
    bool percussion = false;     // true for GM channel 10 / drum-kit parts
    int octaveOffset = 0;        // octaves added at playback (e.g. bass/pad dropped by -1)
    std::optional<YamahaChannelPolicy> yamahaPolicy;
    std::vector<PatternNote> notes;
};

struct Section
{
    std::string name;       // "intro", "mainA", "mainB", "fillAB", "ending"
    int barCount = 4;       // length in bars
    std::vector<Part> parts;
};

struct Style
{
    std::string schema = "cadenza.style.v1";
    std::string id;
    std::string name;
    int defaultTempo = 120;
    int beatsPerBar = 4;
    int beatUnit = 4;        // denominator of time signature
    int ticksPerBeat = 960;  // PPQ
    YamahaStyleFormat yamahaFormat = YamahaStyleFormat::Unknown;
    std::vector<Section> sections;

    const Section* findSection(const std::string& name) const noexcept;
};
}
