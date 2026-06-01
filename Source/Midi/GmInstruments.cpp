#include "GmInstruments.h"

namespace cadenza::midi
{
const char* gmInstrumentName(int program) noexcept
{
    static const char* names[128] = {
        "Acoustic Grand Piano","Bright Acoustic Piano","Electric Grand Piano","Honky-tonk Piano",
        "Electric Piano 1","Electric Piano 2","Harpsichord","Clavinet",
        "Celesta","Glockenspiel","Music Box","Vibraphone","Marimba","Xylophone","Tubular Bells","Dulcimer",
        "Drawbar Organ","Percussive Organ","Rock Organ","Church Organ","Reed Organ","Accordion","Harmonica","Tango Accordion",
        "Nylon Guitar","Steel Guitar","Jazz Guitar","Clean Guitar","Muted Guitar","Overdriven Guitar","Distortion Guitar","Guitar Harmonics",
        "Acoustic Bass","Fingered Bass","Picked Bass","Fretless Bass","Slap Bass 1","Slap Bass 2","Synth Bass 1","Synth Bass 2",
        "Violin","Viola","Cello","Contrabass","Tremolo Strings","Pizzicato Strings","Orchestral Harp","Timpani",
        "String Ensemble 1","String Ensemble 2","Synth Strings 1","Synth Strings 2","Choir Aahs","Voice Oohs","Synth Voice","Orchestra Hit",
        "Trumpet","Trombone","Tuba","Muted Trumpet","French Horn","Brass Section","Synth Brass 1","Synth Brass 2",
        "Soprano Sax","Alto Sax","Tenor Sax","Baritone Sax","Oboe","English Horn","Bassoon","Clarinet",
        "Piccolo","Flute","Recorder","Pan Flute","Blown Bottle","Shakuhachi","Whistle","Ocarina",
        "Square Lead","Sawtooth Lead","Calliope Lead","Chiff Lead","Charang Lead","Voice Lead","Fifths Lead","Bass + Lead",
        "New Age Pad","Warm Pad","Polysynth Pad","Choir Pad","Bowed Pad","Metallic Pad","Halo Pad","Sweep Pad",
        "Rain FX","Soundtrack FX","Crystal FX","Atmosphere FX","Brightness FX","Goblins FX","Echoes FX","Sci-Fi FX",
        "Sitar","Banjo","Shamisen","Koto","Kalimba","Bagpipe","Fiddle","Shanai",
        "Tinkle Bell","Agogo","Steel Drums","Woodblock","Taiko Drum","Melodic Tom","Synth Drum","Reverse Cymbal",
        "Guitar Fret Noise","Breath Noise","Seashore","Bird Tweet","Telephone Ring","Helicopter","Applause","Gunshot"
    };
    if (program < 0 || program > 127) return "";
    return names[program];
}

const char* gmFamilyName(int familyIndex) noexcept
{
    static const char* families[16] = {
        "Piano", "Chromatic Perc.", "Organ", "Guitar",
        "Bass", "Strings", "Ensemble", "Brass",
        "Reed", "Pipe", "Synth Lead", "Synth Pad",
        "Synth FX", "Ethnic", "Percussive", "Sound FX"
    };
    if (familyIndex < 0 || familyIndex > 15) return "";
    return families[familyIndex];
}

int defaultGmProgramForRole(const std::string& role) noexcept
{
    if (role == "bass")    return 33;   // Fingered Bass
    if (role == "chord1")  return 26;   // Jazz Guitar
    if (role == "chord2")  return 0;    // Acoustic Grand Piano
    if (role == "pad")     return 48;   // String Ensemble 1
    if (role == "phrase1") return 61;   // Brass Section
    if (role == "phrase2") return 61;   // Brass Section
    if (role == "harmony") return 0;    // Piano (factory-style label)
    // rhythm2 / other / unknown
    return 0;
}
}
