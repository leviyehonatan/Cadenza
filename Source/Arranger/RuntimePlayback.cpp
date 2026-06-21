#include "RuntimePlayback.h"
#include "../Audio/MidiChannel.h"
#include "../Audio/Transport.h"

#include <algorithm>
#include <cmath>

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

// A per-part register window (MIDI note numbers) — the Cadenza equivalent of a
// Genos channel Note Limit. The nearest-fold transpose already keeps triads
// within ~an octave, so for normal content these windows are no-ops; they catch
// escapees (extreme voicings, 7/9 stacks, future register biasing) and keep each
// part planted in its register instead of scattering or dropping out of range.
struct RegisterWindow { int low; int high; };

RegisterWindow registerWindowForPart(const Part& part) noexcept
{
    const auto role = destinationRoleForPart(part);
    if (role == "pad")     return { 48, 84 };   // C3..C6
    if (role == "harmony") return { 48, 84 };   // C3..C6  keys / guitar stabs
    // Chord comp parts (chord1/chord2/...) sit in the mid keyboard like the keys.
    if (role.rfind("chord", 0) == 0) return { 48, 84 };   // C3..C6
    if (role == "guitar")  return { 45, 81 };   // A2..A5
    if (role == "lead" || role == "melody") return { 55, 91 };  // G3..G6
    return { 40, 88 };                          // generous default, any pitched part
}

// Fold `pitch` by whole octaves until it sits inside [low, high]; clamp to MIDI
// range as a last resort. Notes already inside the window are returned unchanged.
int foldIntoWindow(int pitch, RegisterWindow w) noexcept
{
    while (pitch < w.low  && pitch + 12 <= 127) pitch += 12;
    while (pitch > w.high && pitch - 12 >= 0)   pitch -= 12;
    return std::clamp(pitch, 0, 127);
}
}

int playbackChannelForPart(const Part& part) noexcept
{
    const bool percussion = part.percussion || cadenza::audio::isCadenzaDrumChannel(part.midiChannel);
    if (!percussion)
        return part.midiChannel;
    // RHY2 (Yamaha MIDI ch 9) keeps its own drum channel so it has an independent
    // mixer strip; any other detected percussion joins the main kit on ch 10.
    return part.midiChannel == 9 ? 9 : 10;
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
        // Snap to a valid GM drum kit. Yamaha/Genos kit programs (e.g. 82, 117)
        // don't exist on a GM SoundFont and render as silence or wrong sounds;
        // standard XG kit numbers (Room/Power/Electronic/808/Jazz/Brush/Orchestra)
        // line up with GM and pass through unchanged.
        static const int kGmDrumKits[] = { 0, 8, 16, 24, 25, 32, 40, 48, 56 };
        bool validKit = false;
        for (int k : kGmDrumKits) validKit = validKit || (*setup.program == k);
        if (!validKit)
            setup.program = 0;        // unknown kit -> Standard Kit
    } else {
        // Preserve explicit Yamaha/XG variation banks when the style provides
        // them so XG-capable SoundFonts can use the richer preset. If a melodic
        // part omits bank select entirely, default it to GM 0/0 so the synth does
        // not inherit a stale bank from an earlier part.
        if (!setup.bankMsb)
            setup.bankMsb = 0;
        if (!setup.bankLsb)
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
    // Yamaha/Genos kits use notes outside GM's percussion range (35..81). On a GM
    // SoundFont those land on undefined slots and play garbage (whistle/dog-like
    // SFX) or nothing. Map them to playable GM drums so imported styles groove.

    // 1) Explicit known Yamaha/XG extensions -> closest GM drum (by sound, not by
    //    octave math). The high XG percussion (82..87) must NOT fold down into GM's
    //    whistle/guiro/cuica slots (71..79) — cuica/guiro sound like animal SFX.
    switch (note) {
        case 31: return 37;   // sticks        -> side stick
        case 13: return 36;   // low kick      -> bass drum 1
        case 14: return 37;   // rim           -> side stick
        case 15: return 35;   // kick var      -> acoustic bass drum
        case 82: return 70;   // shaker        -> maracas
        case 83: return 80;   // jingle bell   -> mute triangle
        case 84: return 81;   // bell tree     -> open triangle
        case 85: return 37;   // castanets     -> side stick
        case 86: return 36;   // mute surdo    -> bass drum (keeps the low pulse)
        case 87: return 47;   // open surdo    -> low-mid tom
        default: break;
    }

    // 2) In GM range already: keep as-is.
    if (note >= 35 && note <= 81)
        return note;

    // 3) Anything still out of range: fold low hits up toward the kick/tom zone;
    //    map remaining highs to a crash rather than into the SFX/animal slots.
    if (note > 81)
        return 49;            // unknown high percussion -> crash cymbal
    int n = note;
    while (n < 35) n += 12;   // low hits land in the kick/snare/tom zone (36..46)
    return std::clamp(n, 35, 50);
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

    // Register fence (Genos Note Limit): keep the part planted in its window.
    // Skipped only when the Yamaha policy carries a *real* note limit (the policy
    // path already applied it); a trivial 0..127 (which many imported styles set)
    // is no constraint, so the engine window still applies. Absolute notes (FX)
    // keep their pitch.
    const auto& pol = part.yamahaPolicy;
    const bool hasRealPolicyLimits = pol
        && ((pol->noteLowLimit  && *pol->noteLowLimit  > 0)
         || (pol->noteHighLimit && *pol->noteHighLimit < 127));
    if (!hasRealPolicyLimits && note.role != NoteRole::Absolute)
        played = foldIntoWindow(*played, registerWindowForPart(part));

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

HumanizeProfile humanizeProfileForPart(const Part& part, int amountPercent) noexcept
{
    if (amountPercent <= 0)
        return {};

    const double a = std::clamp(amountPercent, 0, 100) / 100.0;

    // Base (full-amount) spreads per role. Drums get the most velocity life but
    // stay rhythmically tight; bass holds the pocket; comp/keys/guitar breathe the
    // most. Late ticks are tiny (960 ppq => ~0.5ms/tick at 120bpm).
    int vel = 10, late = 6;
    if (part.percussion || part.name == "drums") { vel = 12; late = 4; }
    else if (part.name == "bass")                { vel = 7;  late = 2; }
    else if (part.name == "pad")                 { vel = 6;  late = 3; }
    else                                         { vel = 10; late = 8; }

    HumanizeProfile p;
    p.velocityJitter = static_cast<int>(std::lround(vel * a));
    p.maxLateTicks   = static_cast<int>(std::lround(late * a));
    return p;
}

std::uint32_t humanizeSeed(int tick, int pitch, int partIndex, int loopCounter) noexcept
{
    // FNV-1a over the note's stable identity plus the section-loop counter.
    std::uint32_t h = 2166136261u;
    for (int v : { tick, pitch, partIndex, loopCounter }) {
        h ^= static_cast<std::uint32_t>(v);
        h *= 16777619u;
    }
    return h;
}

int humanizeVelocity(int baseVelocity, std::uint32_t seed, int velocityJitter) noexcept
{
    if (velocityJitter <= 0)
        return std::clamp(baseVelocity, 1, 127);
    const int span  = 2 * velocityJitter + 1;
    const int delta = static_cast<int>(seed % static_cast<std::uint32_t>(span)) - velocityJitter;
    return std::clamp(baseVelocity + delta, 1, 127);
}

int humanizeLateTicks(std::uint32_t seed, int maxLateTicks) noexcept
{
    if (maxLateTicks <= 0)
        return 0;
    // Decorrelate from the velocity draw by mixing the seed first.
    const std::uint32_t s = seed * 2654435761u;
    return static_cast<int>(s % static_cast<std::uint32_t>(maxLateTicks + 1));
}
}
