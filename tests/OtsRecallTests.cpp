// OtsRecall tests — mapping an OtsSetting onto the three right-hand layers.

#include "Arranger/OtsRecall.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
int failures = 0;
void expect(bool cond, const std::string& msg)
{
    if (cond) return;
    ++failures;
    std::cerr << "FAIL: " << msg << '\n';
}

void presentVoicesEnableAndSetLayers()
{
    cadenza::arranger::OtsSetting setting;
    setting.present = true;
    setting.layers[0] = { true, 0, 110 };    // full voice
    setting.layers[1] = { true, 48, -1 };    // program only
    setting.layers[2] = { true, -1, 90 };    // volume only

    const auto t = cadenza::arranger::otsRecallTargets(setting);
    expect(t[0].enabled && t[0].setProgram && t[0].program == 0
               && t[0].setVolume && t[0].volume == 110,
           "full voice sets program and volume");
    expect(t[1].enabled && t[1].setProgram && t[1].program == 48 && !t[1].setVolume,
           "program-only voice leaves volume alone");
    expect(t[2].enabled && !t[2].setProgram && t[2].setVolume && t[2].volume == 90,
           "volume-only voice leaves program alone");
}

void absentVoicesDisableLayers()
{
    cadenza::arranger::OtsSetting setting;
    setting.present = true;
    setting.layers[0] = { true, 24, 100 };

    const auto t = cadenza::arranger::otsRecallTargets(setting);
    expect(t[0].enabled, "defined layer is enabled");
    expect(!t[1].enabled && !t[1].setProgram && !t[1].setVolume,
           "undefined layer 2 is disabled, nothing set");
    expect(!t[2].enabled && !t[2].setProgram && !t[2].setVolume,
           "undefined layer 3 is disabled, nothing set");
}
}

int main()
{
    presentVoicesEnableAndSetLayers();
    absentVoicesDisableLayers();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All OtsRecall tests passed\n";
    return EXIT_SUCCESS;
}
