// GmInstruments — General MIDI instrument naming + role-based default programs.
//
// Maps each style-part role to a sensible General MIDI program (role ->
// GM family -> default program) so each part can be auto-assigned an instrument,
// and the mixer can offer a grouped instrument picker. Based on the standard GM
// instrument families. Pure (no JUCE), in cadenza_core.

#pragma once

#include <string>

namespace cadenza::midi
{
// GM program name, 0..127 (e.g. 0 -> "Acoustic Grand Piano"). "" if out of range.
const char* gmInstrumentName(int program) noexcept;

// The 16 GM families (8 programs each): 0 Piano, 1 Chromatic Percussion, 2 Organ,
// 3 Guitar, 4 Bass, 5 Strings, 6 Ensemble, 7 Brass, 8 Reed, 9 Pipe, 10 Synth Lead,
// 11 Synth Pad, 12 Synth Effects, 13 Ethnic, 14 Percussive, 15 Sound Effects.
const char* gmFamilyName(int familyIndex) noexcept;

// Default GM program for a Cadenza style-part role name (standard GM mapping).
// bass->33, chord1->26(guitar), chord2->0(piano), pad->48(strings),
// phrase1/phrase2->61(brass), harmony->0, rhythm2/other->0. drums handled elsewhere.
int defaultGmProgramForRole(const std::string& role) noexcept;
}
