#include "Arranger/StyleLoader.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;
void expect(bool cond, const std::string& msg) {
    if (cond) return;
    ++failures;
    std::cerr << "FAIL: " << msg << '\n';
}

using namespace cadenza::arranger;

const char* sampleStyleJson = R"({
  "$schema": "cadenza.style.v1",
  "id": "8-beat-pop",
  "name": "8 Beat Pop",
  "tempo": 110,
  "timeSignature": [4, 4],
  "ticksPerBeat": 960,
  "sections": {
    "mainA": {
      "barCount": 2,
      "parts": [
        {
          "name": "drums", "channel": 10, "instrument": "Standard Kit", "program": 0, "volume": 104, "pan": 64, "reverb": 38, "chorus": 6, "percussion": true,
          "notes": [
            { "tick": 0,    "duration": 240, "pitch": 36, "velocity": 110, "role": "absolute" },
            { "tick": 1920, "duration": 240, "pitch": 38, "velocity": 100, "role": "absolute" }
          ]
        },
        {
          "name": "bass", "channel": 2, "instrument": "Acoustic Bass", "bankMsb": 0, "bankLsb": 0, "program": 32,
          "notes": [
            { "tick": 0,    "duration": 480, "pitch": 36, "velocity": 90, "role": "chord-root" },
            { "tick": 960,  "duration": 240, "pitch": 36, "velocity": 80, "role": "chord-5" }
          ],
          "automation": [
            { "tick": 240, "type": 11,  "value": 90 },
            { "tick": 240, "type": 255, "value": 10240 }
          ]
        }
      ]
    }
  }
})";

void pushU8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
void pushU16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(v >> 8); b.push_back(v & 0xFF); }
void pushU32(std::vector<uint8_t>& b, uint32_t v)
{
    b.push_back((v >> 24) & 0xFF);
    b.push_back((v >> 16) & 0xFF);
    b.push_back((v >> 8) & 0xFF);
    b.push_back(v & 0xFF);
}
void pushTag(std::vector<uint8_t>& b, const char* t)
{
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<uint8_t>(t[i]));
}
void pushVLQ(std::vector<uint8_t>& b, uint32_t v)
{
    uint8_t buf[5];
    int n = 0;
    buf[n++] = v & 0x7F;
    v >>= 7;
    while (v) {
        buf[n++] = (v & 0x7F) | 0x80;
        v >>= 7;
    }
    for (int i = n - 1; i >= 0; --i)
        b.push_back(buf[i]);
}
void pushMarker(std::vector<uint8_t>& trk, uint32_t delta, const std::string& text)
{
    pushVLQ(trk, delta);
    pushU8(trk, 0xFF);
    pushU8(trk, 0x06);
    pushVLQ(trk, static_cast<uint32_t>(text.size()));
    for (char c : text)
        trk.push_back(static_cast<uint8_t>(c));
}
void pushTempo(std::vector<uint8_t>& trk, uint32_t delta, uint32_t micros)
{
    pushVLQ(trk, delta);
    pushU8(trk, 0xFF);
    pushU8(trk, 0x51);
    pushVLQ(trk, 3);
    pushU8(trk, (micros >> 16) & 0xFF);
    pushU8(trk, (micros >> 8) & 0xFF);
    pushU8(trk, micros & 0xFF);
}
void pushProgramChange(std::vector<uint8_t>& trk, uint32_t delta, uint8_t channel, uint8_t program)
{
    pushVLQ(trk, delta);
    pushU8(trk, 0xC0 | (channel & 0x0F));
    pushU8(trk, program);
}
void pushControlChange(std::vector<uint8_t>& trk, uint32_t delta, uint8_t channel, uint8_t controller, uint8_t value)
{
    pushVLQ(trk, delta);
    pushU8(trk, 0xB0 | (channel & 0x0F));
    pushU8(trk, controller);
    pushU8(trk, value);
}
void pushPitchBend(std::vector<uint8_t>& trk, uint32_t delta, uint8_t channel, int value14)
{
    pushVLQ(trk, delta);
    pushU8(trk, 0xE0 | (channel & 0x0F));
    pushU8(trk, static_cast<uint8_t>(value14 & 0x7F));          // LSB
    pushU8(trk, static_cast<uint8_t>((value14 >> 7) & 0x7F));   // MSB
}
void pushNoteOn(std::vector<uint8_t>& trk, uint32_t delta, uint8_t channel, uint8_t pitch, uint8_t vel)
{
    pushVLQ(trk, delta);
    pushU8(trk, 0x90 | (channel & 0x0F));
    pushU8(trk, pitch);
    pushU8(trk, vel);
}
void pushNoteOff(std::vector<uint8_t>& trk, uint32_t delta, uint8_t channel, uint8_t pitch)
{
    pushVLQ(trk, delta);
    pushU8(trk, 0x80 | (channel & 0x0F));
    pushU8(trk, pitch);
    pushU8(trk, 64);
}
void pushEndOfTrack(std::vector<uint8_t>& trk, uint32_t delta = 0)
{
    pushVLQ(trk, delta);
    pushU8(trk, 0xFF);
    pushU8(trk, 0x2F);
    pushVLQ(trk, 0);
}
void appendTrack(std::vector<uint8_t>& smf, const std::vector<uint8_t>& trk)
{
    pushTag(smf, "MTrk");
    pushU32(smf, static_cast<uint32_t>(trk.size()));
    smf.insert(smf.end(), trk.begin(), trk.end());
}

std::vector<uint8_t> makeSampleSty()
{
    std::vector<uint8_t> smf;
    pushTag(smf, "MThd");
    pushU32(smf, 6);
    pushU16(smf, 1);
    pushU16(smf, 2);
    pushU16(smf, 480);

    std::vector<uint8_t> markers;
    pushTempo(markers, 0, 500000);
    pushMarker(markers, 0, "Main A");
    pushMarker(markers, 1920, "Main B");
    pushEndOfTrack(markers, 1920);
    appendTrack(smf, markers);

    std::vector<uint8_t> notes;
    pushControlChange(notes, 0, 1, 0, 0);
    pushControlChange(notes, 0, 1, 32, 0);
    pushProgramChange(notes, 0, 1, 32);
    pushControlChange(notes, 0, 9, 7, 110);
    pushControlChange(notes, 0, 9, 10, 62);
    pushControlChange(notes, 0, 9, 91, 48);
    pushControlChange(notes, 0, 9, 93, 9);
    pushNoteOn(notes, 0, 1, 60, 100);
    pushControlChange(notes, 240, 1, 11, 90);   // expression swell mid-note (tick 240)
    pushPitchBend(notes, 0, 1, 10240);          // bend up at tick 240
    pushControlChange(notes, 0, 1, 64, 127);    // sustain on at tick 240
    pushNoteOff(notes, 240, 1, 60);             // tick 480
    pushNoteOn(notes, 0, 1, 64, 100);
    pushNoteOff(notes, 480, 1, 64);
    pushNoteOn(notes, 960, 9, 36, 110);
    pushNoteOff(notes, 240, 9, 36);
    pushEndOfTrack(notes);
    appendTrack(smf, notes);

    return smf;
}

void loadFromJsonSucceeds()
{
    auto r = loadStyleFromJson(sampleStyleJson);
    expect(r.ok, "load ok");
    expect(r.style.id == "8-beat-pop", "id");
    expect(r.style.name == "8 Beat Pop", "name");
    expect(r.style.defaultTempo == 110, "tempo");
    expect(r.style.beatsPerBar == 4 && r.style.beatUnit == 4, "time signature");
    expect(r.style.ticksPerBeat == 960, "ppq");

    expect(r.style.sections.size() == 1, "one section");
    const auto* main = r.style.findSection("mainA");
    expect(main != nullptr, "mainA found");
    expect(main->barCount == 2, "mainA bar count");
    expect(main->parts.size() == 2, "mainA has 2 parts");

    const auto& drums = main->parts[0];
    expect(drums.name == "drums", "drums name");
    expect(drums.midiChannel == 10, "drums channel");
    expect(drums.percussion, "drums percussion flag");
    expect(drums.program && *drums.program == 0, "drums program");
    expect(drums.volume && *drums.volume == 104, "drums volume CC7");
    expect(drums.pan && *drums.pan == 64, "drums pan CC10");
    expect(drums.reverb && *drums.reverb == 38, "drums reverb CC91");
    expect(drums.chorus && *drums.chorus == 6, "drums chorus CC93");
    expect(drums.notes.size() == 2, "drums notes");

    const auto& bass = main->parts[1];
    expect(bass.bankMsb && *bass.bankMsb == 0, "bass bank MSB");
    expect(bass.bankLsb && *bass.bankLsb == 0, "bass bank LSB");
    expect(bass.program && *bass.program == 32, "bass program");
    expect(bass.notes[0].role == NoteRole::ChordRoot, "bass note 0 role");
    expect(bass.notes[1].role == NoteRole::Chord5, "bass note 1 role");
    expect(bass.automation.size() == 2, "bass automation loaded");
    if (bass.automation.size() == 2) {
        expect(bass.automation[0].type == 11 && bass.automation[0].value == 90, "bass CC11 automation");
        expect(bass.automation[1].type == AutomationEvent::kPitchBend
               && bass.automation[1].value == 10240, "bass pitch-bend automation");
    }
}

void roleStringRoundTrip()
{
    expect(roleFromString("chord-root") == NoteRole::ChordRoot, "chord-root");
    expect(roleFromString("chord-3") == NoteRole::Chord3, "chord-3");
    expect(roleFromString("chord-5") == NoteRole::Chord5, "chord-5");
    expect(roleFromString("chord-7") == NoteRole::Chord7, "chord-7");
    expect(roleFromString("scale-tone") == NoteRole::ScaleTone, "scale-tone");
    expect(roleFromString("absolute") == NoteRole::Absolute, "absolute");
    expect(roleFromString("nonsense") == NoteRole::Absolute, "fallback");

    expect(std::string(roleToString(NoteRole::ChordRoot)) == "chord-root", "ChordRoot->string");
    expect(std::string(roleToString(NoteRole::ScaleTone)) == "scale-tone", "ScaleTone->string");
}

void saveAndReload()
{
    auto loaded = loadStyleFromJson(sampleStyleJson);
    expect(loaded.ok, "initial load");

    auto json = saveStyleToJson(loaded.style, false);
    auto reloaded = loadStyleFromJson(json);
    expect(reloaded.ok, "reload ok");
    expect(reloaded.style.id == loaded.style.id, "id round-trip");
    expect(reloaded.style.sections.size() == loaded.style.sections.size(), "section count round-trip");
    const auto* a = loaded.style.findSection("mainA");
    const auto* b = reloaded.style.findSection("mainA");
    expect(a && b && a->parts.size() == b->parts.size(), "part count round-trip");
    if (b && !b->parts.empty()) {
        const auto& drums = b->parts[0];
        expect(drums.volume && *drums.volume == 104, "volume round-trip");
        expect(drums.pan && *drums.pan == 64, "pan round-trip");
        expect(drums.reverb && *drums.reverb == 38, "reverb round-trip");
        expect(drums.chorus && *drums.chorus == 6, "chorus round-trip");
    }
    if (b && b->parts.size() >= 2) {
        const auto& bass = b->parts[1];
        expect(bass.automation.size() == 2, "automation count round-trip");
        if (bass.automation.size() == 2) {
            expect(bass.automation[1].type == AutomationEvent::kPitchBend
                   && bass.automation[1].value == 10240, "pitch-bend round-trip");
        }
    }
}

void loadFromCstyleFileSucceeds()
{
    const auto path = std::filesystem::temp_directory_path() / "cadenza-style-loader-test.cstyle";
    {
        std::ofstream out(path, std::ios::binary);
        out << sampleStyleJson;
    }

    auto r = loadStyleFromFile(path.string());
    std::filesystem::remove(path);

    expect(r.ok, "cstyle load ok");
    if (!r.ok)
        return;

    expect(r.style.id == "8-beat-pop", "cstyle id");
    expect(r.style.findSection("mainA") != nullptr, "cstyle mainA section");
}

void malformedJsonFails()
{
    auto r = loadStyleFromJson("{not even json}");
    expect(!r.ok, "malformed rejected");
}

void loadFromStyFileSucceeds()
{
    const auto path = std::filesystem::temp_directory_path() / "cadenza-style-loader-test.sty";
    {
        std::ofstream out(path, std::ios::binary);
        const auto bytes = makeSampleSty();
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    auto r = loadStyleFromStyFile(path.string());
    std::filesystem::remove(path);

    expect(r.ok, "sty load ok");
    if (!r.ok) {
        std::cerr << r.error << '\n';
        return;
    }

    expect(r.style.ticksPerBeat == 480, "sty ppq");
    expect(r.style.defaultTempo == 120, "sty tempo");
    expect(r.style.findSection("mainA") != nullptr, "sty mainA section");
    expect(r.style.findSection("mainB") != nullptr, "sty mainB section");

    const auto* mainA = r.style.findSection("mainA");
    const Part* bass = nullptr;
    for (const auto& part : mainA->parts)
        if (part.midiChannel == 2)
            bass = &part;

    expect(bass != nullptr, "sty bass part");
    if (bass != nullptr) {
        expect(bass->notes.size() == 2, "sty bass notes");
        expect(bass->bankMsb && *bass->bankMsb == 0, "sty bass bank MSB");
        expect(bass->bankLsb && *bass->bankLsb == 0, "sty bass bank LSB");
        expect(bass->program && *bass->program == 32, "sty bass program");
        expect(bass->notes[0].role == NoteRole::ChordRoot, "sty bass follows chord root");
        expect(bass->notes[1].role == NoteRole::Chord3, "sty harmony follows chord third");

        // Expression / sustain / pitch-bend captured from the .sty MIDI stream.
        bool sawExpr = false, sawSustain = false, sawBend = false;
        for (const auto& a : bass->automation) {
            if (a.type == 11 && a.value == 90 && a.tick == 240) sawExpr = true;
            if (a.type == 64 && a.value == 127 && a.tick == 240) sawSustain = true;
            if (a.type == AutomationEvent::kPitchBend && a.value == 10240 && a.tick == 240) sawBend = true;
        }
        expect(bass->automation.size() == 3, "sty bass automation captured");
        expect(sawExpr, "sty bass expression CC11 captured");
        expect(sawSustain, "sty bass sustain CC64 captured");
        expect(sawBend, "sty bass pitch-bend captured");
    }

    const auto* mainB = r.style.findSection("mainB");
    const Part* drums = nullptr;
    for (const auto& part : mainB->parts)
        if (part.midiChannel == 10)
            drums = &part;

    expect(drums != nullptr, "sty drums part");
    if (drums != nullptr) {
        expect(drums->percussion, "sty drums percussion flag");
        expect(drums->volume && *drums->volume == 110, "sty drums volume CC7");
        expect(drums->pan && *drums->pan == 62, "sty drums pan CC10");
        expect(drums->reverb && *drums->reverb == 48, "sty drums reverb CC91");
        expect(drums->chorus && *drums->chorus == 9, "sty drums chorus CC93");
        expect(drums->notes[0].role == NoteRole::Absolute, "sty drums stay absolute");
    }
}
}

int main()
{
    loadFromJsonSucceeds();
    roleStringRoundTrip();
    saveAndReload();
    loadFromCstyleFileSucceeds();
    malformedJsonFails();
    loadFromStyFileSucceeds();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All StyleLoader tests passed\n";
    return EXIT_SUCCESS;
}
