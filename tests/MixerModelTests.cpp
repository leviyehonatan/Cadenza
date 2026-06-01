#include "Audio/MixerModel.h"

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

using cadenza::audio::MixerModel;

void buildsStripsWithDefaults()
{
    MixerModel m;
    m.setChannels({ 1, 2, 10 });
    expect(m.channels().size() == 3, "three strips");
    expect(m.has(1) && m.has(2) && m.has(10), "channels present");
    expect(m.volume(1) == 100, "default volume 100");
    expect(!m.mute(2) && !m.solo(2), "defaults: not muted/soloed");
    expect(m.effectiveVolume(1) == 100, "default effective = volume");
}

void volumeClamps()
{
    MixerModel m;
    m.setChannels({ 1 });
    m.setVolume(1, 200);
    expect(m.volume(1) == 127, "clamp high");
    m.setVolume(1, -5);
    expect(m.volume(1) == 0, "clamp low");
}

void muteSilencesChannel()
{
    MixerModel m;
    m.setChannels({ 1, 2 });
    m.setMute(1, true);
    expect(m.effectiveVolume(1) == 0, "muted channel is silent");
    expect(m.effectiveVolume(2) == 100, "other channel unaffected");
}

void soloOverridesOthers()
{
    MixerModel m;
    m.setChannels({ 1, 2, 10 });
    m.setVolume(2, 90);
    m.setSolo(2, true);
    expect(m.anySolo(), "anySolo true");
    expect(m.effectiveVolume(2) == 90, "soloed channel plays");
    expect(m.effectiveVolume(1) == 0, "non-soloed silenced while a solo is active");
    expect(m.effectiveVolume(10) == 0, "non-soloed silenced (2)");

    // Multiple solos: all soloed channels are heard.
    m.setSolo(10, true);
    expect(m.effectiveVolume(10) == 100, "second soloed channel also plays");
    expect(m.effectiveVolume(1) == 0, "still-non-soloed stays silent");

    // Clearing solos restores normal playback.
    m.setSolo(2, false);
    m.setSolo(10, false);
    expect(!m.anySolo(), "no solo after clearing");
    expect(m.effectiveVolume(1) == 100, "all audible again");
}

void mutedStaysSilentEvenIfSoloed()
{
    MixerModel m;
    m.setChannels({ 1 });
    m.setMute(1, true);
    m.setSolo(1, true);
    expect(m.effectiveVolume(1) == 0, "mute wins over solo");
}

void rebuildPreservesExistingState()
{
    MixerModel m;
    m.setChannels({ 1, 2 });
    m.setVolume(2, 40);
    m.setMute(2, true);
    m.setProgram(2, 48);

    m.setChannels({ 2, 3 });   // channel 1 dropped, 3 added, 2 kept
    expect(m.has(2) && m.has(3) && !m.has(1), "rebuilt channel set");
    expect(m.volume(2) == 40, "kept channel preserves volume");
    expect(m.mute(2), "kept channel preserves mute");
    expect(m.program(2) == 48, "kept channel preserves program");
    expect(m.volume(3) == 100, "new channel gets default");
    expect(m.program(3) == 0, "new channel default program 0");
}

void programGetSetClamps()
{
    MixerModel m;
    m.setChannels({ 1 });
    m.setProgram(1, 61);
    expect(m.program(1) == 61, "program set");
    m.setProgram(1, 999);
    expect(m.program(1) == 127, "program clamps high");
    m.setProgram(1, -3);
    expect(m.program(1) == 0, "program clamps low");
}
}

int main()
{
    buildsStripsWithDefaults();
    volumeClamps();
    muteSilencesChannel();
    soloOverridesOthers();
    mutedStaysSilentEvenIfSoloed();
    rebuildPreservesExistingState();
    programGetSetClamps();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All MixerModel tests passed\n";
    return EXIT_SUCCESS;
}
