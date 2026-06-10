// style-probe — load a .sty/.cstyle, and for a given chord print what each part
// would actually PLAY (source pitch -> played pitch + role) so transposition
// bugs (wrong octave / offset / messy voicing) are visible at a glance.
//
// Usage: style-probe <file.sty|.cstyle> [chord=C] [section] [notesPerPart=10]

#include "Arranger/StyParser.h"
#include "Arranger/StyleLoader.h"
#include "Arranger/RuntimePlayback.h"
#include "Arranger/PatternTransposer.h"
#include "Arranger/PlaybackDiagnostics.h"
#include "Arranger/Style.h"
#include "Midi/ChordRecognizer.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <optional>
#include <string>

using namespace cadenza::arranger;

static std::string noteName(int midi)
{
    static const char* n[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    char buf[8];
    if (midi < 0 || midi > 127) return "--";
    std::snprintf(buf, sizeof buf, "%s%d", n[midi % 12], midi / 12 - 1);
    return buf;
}

static const char* roleName(NoteRole r)
{
    switch (r) {
        case NoteRole::Absolute:   return "abs";
        case NoteRole::ChordRoot:  return "root";
        case NoteRole::Chord3:     return "3rd";
        case NoteRole::Chord5:     return "5th";
        case NoteRole::Chord7:     return "7th";
        case NoteRole::ChordColor: return "color";
        case NoteRole::ScaleTone:  return "scale";
    }
    return "?";
}

static const char* ntrName(YamahaNtr ntr)
{
    switch (ntr) {
        case YamahaNtr::RootFixed: return "RootFixed";
        case YamahaNtr::RootTransposition: return "RootTransposition";
        case YamahaNtr::Guitar: return "Guitar";
        case YamahaNtr::Unknown: return "Unknown";
    }
    return "Unknown";
}

static const char* nttName(YamahaNtt ntt)
{
    switch (ntt) {
        case YamahaNtt::Bypass: return "Bypass";
        case YamahaNtt::Melody: return "Melody";
        case YamahaNtt::Chord: return "Chord";
        case YamahaNtt::MelodicMinor: return "MelodicMinor";
        case YamahaNtt::HarmonicMinor: return "HarmonicMinor";
        case YamahaNtt::NaturalMinor: return "NaturalMinor";
        case YamahaNtt::Dorian: return "Dorian";
        case YamahaNtt::AllPurpose: return "AllPurpose";
        case YamahaNtt::Stroke: return "Stroke";
        case YamahaNtt::Arpeggio: return "Arpeggio";
        case YamahaNtt::Unknown: return "Unknown";
    }
    return "Unknown";
}

static int valueOrMinusOne(const std::optional<int>& value)
{
    return value.value_or(-1);
}

static void printOts(const Style& style)
{
    bool any = false;
    for (const auto& slot : style.ots)
        if (slot.present) any = true;
    if (!any) {
        std::printf("OTS: none\n");
        return;
    }
    std::printf("OTS:\n");
    static const char* layerNames[3] = { "R1", "R2", "R3" };
    for (std::size_t s = 0; s < style.ots.size(); ++s) {
        const auto& slot = style.ots[s];
        if (!slot.present) {
            std::printf("  slot %zu: --\n", s + 1);
            continue;
        }
        std::printf("  slot %zu:", s + 1);
        for (std::size_t i = 0; i < slot.layers.size(); ++i) {
            const auto& v = slot.layers[i];
            if (v.present)
                std::printf("  %s prog %d vol %d", layerNames[i], v.program, v.volume);
            else
                std::printf("  %s --", layerNames[i]);
        }
        std::printf("\n");
    }
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: style-probe <file.sty|.cstyle> [chord=C] [section] [n=10]\n"); return 2; }

    const std::string path = argv[1];
    bool dumpCasm = false;
    std::vector<std::string> rest;
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--casm") dumpCasm = true;
        else rest.push_back(argv[i]);
    }
    const std::string chordStr = rest.size() > 0 ? rest[0] : "C";
    const std::string sectionWanted = rest.size() > 1 ? rest[1] : "";
    const int perPart = rest.size() > 2 ? std::atoi(rest[2].c_str()) : 10;

    Style style;
    const bool isSty = path.size() > 4 &&
        (path.substr(path.size() - 4) == ".sty" || path.substr(path.size() - 4) == ".STY");
    if (isSty) {
        auto r = parseStyFile(path);
        if (!r.ok) { std::printf("parse failed: %s\n", r.error.c_str()); return 1; }
        if (dumpCasm) {
            const auto& casm = r.casm;
            std::printf("CASM: found=%d csegs=%zu ctabEntries=%zu warnings=%zu\n",
                        (int) casm.found, casm.csegs.size(), casm.ctabEntryCount, casm.warnings.size());
            for (const auto& cseg : casm.csegs) {
                std::printf("  CSEG section='%s' id='%s' entries=%zu\n",
                            cseg.sectionName.value_or("(none)").c_str(),
                            cseg.sectionId.c_str(), cseg.ctabEntries.size());
                for (const auto& e : cseg.ctabEntries) {
                    std::printf("    ch=%s ntrRaw=%d nttRaw=%d ntr='%s' ntt='%s' policy=%s",
                        e.channel ? std::to_string(*e.channel).c_str() : "?",
                        e.ntrRaw ? (int) *e.ntrRaw : -1, e.nttRaw ? (int) *e.nttRaw : -1,
                        e.ntr.value_or("-").c_str(), e.ntt.value_or("-").c_str(),
                        e.policy ? "YES" : "no");
                    if (e.policy)
                        std::printf(" [%s/%s rawNtr=%d rawNtt=%d bass=%d]",
                            ntrName(e.policy->ntr), nttName(e.policy->ntt),
                            e.policy->rawNtr ? (int) *e.policy->rawNtr : -1,
                            e.policy->rawNtt ? (int) *e.policy->rawNtt : -1,
                            (int) e.policy->bassOn);
                    std::printf(" rawLen=%zu\n", e.raw.size());
                    std::printf("      raw:");
                    for (std::size_t bi = 0; bi < e.raw.size() && bi < 40; ++bi)
                        std::printf(" %02X", e.raw[bi]);
                    std::printf("\n");
                }
            }
            return 0;
        }
        style = std::move(r.style);
    } else {
        auto r = loadStyleFromFile(path);
        if (!r.ok) { std::printf("load failed: %s\n", r.error.c_str()); return 1; }
        style = std::move(r.style);
    }

    auto chord = cadenza::midi::parseChordSymbol(chordStr);
    if (!chord) { std::printf("bad chord: %s\n", chordStr.c_str()); return 1; }

    std::printf("Style: %s  ppq=%d  sections=%zu  chord=%s(root=%d)\n",
                style.name.c_str(), style.ticksPerBeat, style.sections.size(),
                chordStr.c_str(), chord->rootPitchClass);
    if (style.parseWarnings.empty()) {
        std::printf("Diagnostics: none\n");
    } else {
        std::printf("Diagnostics: %zu warning%s\n",
                    style.parseWarnings.size(),
                    style.parseWarnings.size() == 1 ? "" : "s");
        for (const auto& warning : style.parseWarnings)
            std::printf("- warning: %s\n", warning.c_str());
    }
    printOts(style);

    const TransposeContext ctx = makeStylePlaybackContext(*chord, /*key=*/0, /*transpose=*/0);

    for (const auto& section : style.sections) {
        if (!sectionWanted.empty() && section.name != sectionWanted) continue;
        std::printf("\n== section %s (bars=%d, parts=%zu) ==\n",
                    section.name.c_str(), section.barCount, section.parts.size());
        std::map<int, int> playbackChannelUseCount;
        for (const auto& part : section.parts)
            ++playbackChannelUseCount[playbackChannelForPart(part)];
        for (const auto& setup : playbackSetupsForSection(section)) {
            if (playbackChannelUseCount[setup.cadenzaChannel] > 1) {
                std::printf("  channel %d setup owner: %s bankMsb=%d program=%d\n",
                            setup.cadenzaChannel,
                            setup.partName.c_str(),
                            valueOrMinusOne(setup.bankMsb),
                            valueOrMinusOne(setup.program));
            }
        }
        for (const auto& part : section.parts) {
            const auto setup = playbackSetupForPart(part);
            const char* role = part.name.c_str();
            if (part.yamahaPolicy && !part.yamahaPolicy->destinationPart.empty())
                role = part.yamahaPolicy->destinationPart.c_str();
            std::printf("  part '%s' srcCh=%d role=%s playCh=%d prog=%d perc=%d notes=%zu\n",
                        part.name.c_str(), setup.sourceChannel, role, setup.cadenzaChannel,
                        setup.program.value_or(-1), (int) setup.percussion, part.notes.size());
            if (part.yamahaPolicy) {
                const auto& p = *part.yamahaPolicy;
                std::printf("      policy srcCh=%d dest=%s ntr=%s ntt=%s bassOn=%d noteRange=%d..%d chordRootUpperLimit=%d\n",
                            p.sourceChannel,
                            p.destinationPart.empty() ? part.name.c_str() : p.destinationPart.c_str(),
                            ntrName(p.ntr),
                            nttName(p.ntt),
                            (int) p.bassOn,
                            valueOrMinusOne(p.noteLowLimit),
                            valueOrMinusOne(p.noteHighLimit),
                            valueOrMinusOne(p.chordRootUpperLimit));
            }

            int shown = 0, lo = 999, hi = -1;
            for (const auto& note : part.notes) {
                auto played = playbackNoteForPart(part, note, ctx);
                if (played) { lo = std::min(lo, *played); hi = std::max(hi, *played); }
                if (shown < perPart) {
                    const auto sourceName = noteName(note.pitch);
                    const auto playedName = played ? noteName(*played) : std::string("--");
                    std::printf("      t=%-5d src=%-4s(%3d) -> %-4s(%3d)  %s\n",
                                note.tick, sourceName.c_str(), note.pitch,
                                playedName.c_str(), played.value_or(-1),
                                roleName(note.role));
                    ++shown;
                }
            }
            if (hi >= 0) {
                const auto loName = noteName(lo);
                const auto hiName = noteName(hi);
                std::printf("      [played range %s(%d)..%s(%d)]\n", loName.c_str(), lo, hiName.c_str(), hi);
            }
        }
        if (!sectionWanted.empty()) break;
    }

    // Render the chosen section (or mainA) at this chord to a MIDI file you can
    // play in any MIDI player to judge the raw arrangement (rhythm + voicing).
    {
        std::string sec = sectionWanted.empty() ? std::string("mainA") : sectionWanted;
        if (!style.findSection(sec) && !style.sections.empty())
            sec = style.sections.front().name;
        const std::string outDir = "probe_out";
        auto r = exportPlaybackDiagnostics(style, sec, ctx, outDir, 8);
        if (r.ok)
            std::printf("\nRendered %d events of section '%s' at chord %s to:\n  %s\n",
                        r.eventCount, sec.c_str(), chordStr.c_str(), r.midiPath.c_str());
        else
            std::printf("\nMIDI render failed: %s\n", r.error.c_str());
    }
    return 0;
}
