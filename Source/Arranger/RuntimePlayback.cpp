#include "RuntimePlayback.h"
#include "../Audio/MidiChannel.h"
#include "../Audio/Transport.h"

#include <algorithm>

namespace cadenza::arranger
{
namespace
{
bool hasKnownYamahaPolicy(const Part& part)
{
    return part.yamahaPolicy && part.yamahaPolicy->source != YamahaPolicySource::Fallback;
}

std::string destinationRoleForPart(const Part& part)
{
    if (part.yamahaPolicy && !part.yamahaPolicy->destinationPart.empty())
        return part.yamahaPolicy->destinationPart;
    return part.name;
}

int channelSetupPriority(const Part& part) noexcept
{
    const bool percussion = part.percussion || cadenza::audio::isCadenzaDrumChannel(part.midiChannel);
    if (!percussion)
        return 0;

    const auto role = destinationRoleForPart(part);
    if (role == "drums")
        return 30;
    if (role == "rhythm2")
        return 20;
    return 10;
}
}

int playbackChannelForPart(const Part& part) noexcept
{
    const bool percussion = part.percussion || cadenza::audio::isCadenzaDrumChannel(part.midiChannel);
    return percussion ? 10 : part.midiChannel;
}

namespace
{
// Gentle stereo placement by role (0..127, 64 = center). Bass and drums stay
// centered (standard mixing); comping/phrase parts spread left/right.
int defaultPanForPart(const std::string& name) noexcept
{
    if (name == "chord1" || name == "phrase1" || name == "guitar")  return 48;
    if (name == "chord2" || name == "phrase2")                      return 80;
    if (name == "pad" || name == "harmony" || name == "strings")    return 54;
    return 64;  // drums, bass, rhythm2, melody, unknown
}

int defaultChorusForPart(const std::string& name) noexcept
{
    if (name == "pad" || name == "chord1" || name == "chord2")  return 16;
    return 0;
}
}

PartPlaybackSetup playbackSetupForPart(const Part& part)
{
    PartPlaybackSetup setup;
    setup.partName = part.name;
    setup.sourceChannel = part.midiChannel;
    setup.cadenzaChannel = playbackChannelForPart(part);
    setup.synthChannel = cadenza::audio::synthChannelFromCadenzaChannel(setup.cadenzaChannel);
    setup.bankMsb = part.bankMsb;
    setup.bankLsb = part.bankLsb;
    setup.program = part.program;
    setup.volume = part.volume;
    setup.pan = part.pan;
    setup.reverb = part.reverb;
    setup.chorus = part.chorus;
    setup.percussion = part.percussion || cadenza::audio::isCadenzaDrumChannel(part.midiChannel);
    if (setup.percussion) {
        if (!setup.program)
            setup.program = 0;        // default GM drum kit
    } else {
        // Melodic voice on a General MIDI SoundFont: Yamaha XG/GS *variation*
        // banks (bank LSB 112/117/19, etc. from .sty files) are not present in a
        // GM SoundFont, so forwarding them makes FluidSynth load the wrong (or no)
        // preset. The program number already matches the GM instrument (Yamaha XG
        // voices are GM-program-aligned), so force GM bank 0 and let the program
        // pick the right instrument family.
        setup.bankMsb = 0;
        setup.bankLsb = 0;
    }
    // Mix defaults so the band sounds wide and natural instead of dry and dead-
    // center. Volume/pan/chorus fill in only when the style omits them.
    if (!setup.volume) setup.volume = 100;
    if (!setup.pan)    setup.pan    = defaultPanForPart(part.name);
    if (!setup.chorus) setup.chorus = defaultChorusForPart(part.name);

    // Reverb FLOOR (not just a default): many styles set CC91=0 and rely on the
    // keyboard's always-on global reverb for ambience. Raise anything below the
    // floor so no part plays bone-dry; richer style values are kept as-is.
    const int reverbFloor = setup.percussion ? 12 : 25;
    setup.reverb = std::max(setup.reverb.value_or(reverbFloor), reverbFloor);

    setup.noteCount = static_cast<int>(part.notes.size());
    return setup;
}

std::vector<PartPlaybackSetup> playbackSetupsForSection(const Section& section)
{
    struct ChosenSetup
    {
        PartPlaybackSetup setup;
        int priority = 0;
    };

    std::vector<ChosenSetup> chosen;
    for (const auto& part : section.parts) {
        const auto setup = playbackSetupForPart(part);
        const int priority = channelSetupPriority(part);

        auto it = std::find_if(chosen.begin(), chosen.end(), [&](const ChosenSetup& item) {
            return item.setup.cadenzaChannel == setup.cadenzaChannel;
        });

        if (it == chosen.end()) {
            chosen.push_back({ setup, priority });
            continue;
        }

        if (priority > it->priority)
            *it = { setup, priority };
    }

    std::vector<PartPlaybackSetup> result;
    result.reserve(chosen.size());
    for (const auto& item : chosen)
        result.push_back(item.setup);
    return result;
}

void applyStyleTimingToTransport(cadenza::audio::Transport& transport,
                                 const Style& style,
                                 bool applyTempo)
{
    transport.setTicksPerBeat(style.ticksPerBeat);
    transport.setTimeSignature(style.beatsPerBar, style.beatUnit);
    if (applyTempo)
        transport.setBpm(static_cast<double>(style.defaultTempo));
}

bool isYamahaXgDrumPart(const Part& part)
{
    const bool percussion = part.percussion || cadenza::audio::isCadenzaDrumChannel(part.midiChannel);
    if (!percussion)
        return false;

    return (part.bankMsb && *part.bankMsb == 127) || hasKnownYamahaPolicy(part);
}

int remapYamahaXgToGmDrumNote(int note) noexcept
{
    // Observed in local Yamaha .sty audit outside Cadenza's common GM range.
    // Yamaha/XG note 31 is "sticks"; GM side stick is the closest safe target.
    if (note == 31)
        return 37;

    return note;
}

DrumNoteRemap drumNoteForPlayback(const Part& part, int note)
{
    DrumNoteRemap result;
    result.originalNote = note;
    result.playbackNote = note;
    result.yamahaXg = isYamahaXgDrumPart(part);

    if (result.yamahaXg)
        result.playbackNote = remapYamahaXgToGmDrumNote(note);

    result.remapped = result.playbackNote != result.originalNote;
    return result;
}

std::optional<int> playbackNoteForPart(const Part& part,
                                       const PatternNote& note,
                                       const TransposeContext& context)
{
    const bool percussion = part.percussion || cadenza::audio::isCadenzaDrumChannel(part.midiChannel);
    if (percussion)
        return drumNoteForPlayback(part, note.pitch).playbackNote;

    auto played = transposeNote(note, context, part.yamahaPolicy ? &*part.yamahaPolicy : nullptr);
    if (!played)
        return played;

    // Bass: anchor the note to one consistent low octave (E1..Eb2). Otherwise the
    // nearest-tone chord fold can send some roots an octave down (e.g. G -> G0),
    // making the foundation jump register or vanish so the chord "sounds wrong".
    const bool isBass = part.name == "bass"
                     || (part.yamahaPolicy && part.yamahaPolicy->bassOn);
    if (isBass && note.role != NoteRole::Absolute) {
        const int pc = ((*played % 12) + 12) % 12;
        return 28 + ((pc - 4 + 12) % 12);   // 28 = E1; window [28, 40)
    }

    // Per-part octave placement (e.g. the pad dropped an octave for a fuller body).
    if (part.octaveOffset != 0) {
        const int shifted = *played + 12 * part.octaveOffset;
        if (shifted >= 0 && shifted <= 127)
            played = shifted;   // keep original octave if the shift would clip out of range
    }
    return played;
}

TransposeContext makeStylePlaybackContext(const cadenza::midi::Chord& chord,
                                          int keyTonicPc,
                                          int transposeSemitones) noexcept
{
    TransposeContext ctx;
    ctx.chord = chord;
    ctx.keyTonicPC = keyTonicPc;
    ctx.globalTranspose = transposeSemitones;
    ctx.globalOctave = 0;   // Octave never affects style/accompaniment parts.
    return ctx;
}

int liveMelodyNote(int note, int octaves) noexcept
{
    int n = note + 12 * octaves;
    if (n < 0)   n = 0;
    if (n > 127) n = 127;
    return n;
}
}
