#include "Midi/GmInstruments.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
int failures = 0;
void expect(bool cond, const std::string& msg) {
    if (cond) return;
    ++failures;
    std::cerr << "FAIL: " << msg << '\n';
}

using namespace cadenza::midi;

void namesAndFamilies()
{
    expect(std::string(gmInstrumentName(0)) == "Acoustic Grand Piano", "program 0 name");
    expect(std::string(gmInstrumentName(33)) == "Fingered Bass", "program 33 name");
    expect(std::string(gmInstrumentName(48)) == "String Ensemble 1", "program 48 name");
    expect(std::string(gmInstrumentName(128)) == "", "out-of-range program -> empty");
    expect(std::string(gmFamilyName(4)) == "Bass", "family 4 = Bass");
    expect(std::string(gmFamilyName(3)) == "Guitar", "family 3 = Guitar");
    expect(std::string(gmFamilyName(99)) == "", "out-of-range family -> empty");
}

void roleDefaultsMatchJJazzLab()
{
    expect(defaultGmProgramForRole("bass") == 33,    "bass -> Fingered Bass");
    expect(defaultGmProgramForRole("chord1") == 26,  "chord1 -> Jazz Guitar");
    expect(defaultGmProgramForRole("chord2") == 0,   "chord2 -> Piano");
    expect(defaultGmProgramForRole("pad") == 48,     "pad -> Strings");
    expect(defaultGmProgramForRole("phrase1") == 61, "phrase1 -> Brass");
    expect(defaultGmProgramForRole("phrase2") == 61, "phrase2 -> Brass");
    expect(defaultGmProgramForRole("harmony") == 0,  "harmony -> Piano");
    expect(defaultGmProgramForRole("anything-else") == 0, "unknown -> Piano");
}
}

int main()
{
    namesAndFamilies();
    roleDefaultsMatchJJazzLab();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All GmInstruments tests passed\n";
    return EXIT_SUCCESS;
}
