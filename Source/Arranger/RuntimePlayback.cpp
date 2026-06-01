#include "RuntimePlayback.h"
#include "../Audio/MidiChannel.h"
#include "../Audio/Transport.h"

namespace cadenza::arranger
{
namespace
{
bool hasKnownYamahaPolicy(const Part& part)
{
    return part.yamahaPolicy && part.yamahaPolicy->source != YamahaPolicySource::Fallback;
}
}

PartPlaybackSetup playbackSetupForPart(const Part& part)
{
    PartPlaybackSetup setup;
    setup.partName = part.name;
    setup.cadenzaChannel = part.midiChannel;
    setup.synthChannel = cadenza::audio::synthChannelFromCadenzaChannel(part.midiChannel);
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
    setup.noteCount = static_cast<int>(part.notes.size());
    return setup;
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
