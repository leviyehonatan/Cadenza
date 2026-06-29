#include "UI/FirstLaunchChecklist.h"

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

using namespace cadenza::ui;

FirstLaunchInputs allGood()
{
    FirstLaunchInputs in;
    in.audioReady = true;      in.audioDeviceName = "Speakers";
    in.synthAvailable = true;
    in.soundFontLoaded = true; in.soundFontName = "GeneralUser GS 1.35.sf2";
    in.styleLoaded = true;     in.styleName = "8 Beat Pop";
    in.midiConnected = true;   in.midiDeviceName = "MIDI Keyboard";
    return in;
}

void checklistHasFourItemsInOrder()
{
    const auto items = buildFirstLaunchChecklist(allGood());
    expect(items.size() == 4, "checklist has four rows");
    expect(items[0].label == "Audio output", "row 0 is audio");
    expect(items[1].label == "Sound (SoundFont)", "row 1 is sound");
    expect(items[2].label == "Style", "row 2 is style");
    expect(items[3].label == "MIDI keyboard", "row 3 is midi");
    expect(items[0].required && items[1].required && items[2].required, "audio/sound/style are required");
    expect(!items[3].required, "midi is optional");
}

void allGoodIsReadyAndShowsDetails()
{
    const auto items = buildFirstLaunchChecklist(allGood());
    expect(allRequiredReady(items), "all-good state is ready");
    expect(items[0].ok && items[1].ok && items[2].ok && items[3].ok, "every row ok");
    expect(items[1].detail == "GeneralUser GS 1.35.sf2", "soundfont shows its file name");
    expect(items[2].detail == "8 Beat Pop", "style shows its name");
}

void missingSoundFontBlocksReady()
{
    auto in = allGood();
    in.soundFontLoaded = false; in.soundFontName.clear();
    const auto items = buildFirstLaunchChecklist(in);
    expect(!items[1].ok, "soundfont row not ok when none loaded");
    expect(!allRequiredReady(items), "missing soundfont is not ready");
    expect(!items[1].detail.empty(), "soundfont row gives a fix hint");
}

void nullSynthBlocksAudioEvenWithDevice()
{
    auto in = allGood();
    in.synthAvailable = false;   // device open, but silent Null fallback
    const auto items = buildFirstLaunchChecklist(in);
    expect(!items[0].ok, "audio row not ok with Null synth");
    expect(!allRequiredReady(items), "Null synth is not ready");
}

void noAudioDeviceBlocksReady()
{
    auto in = allGood();
    in.audioReady = false;
    const auto items = buildFirstLaunchChecklist(in);
    expect(!items[0].ok, "audio row not ok with no device");
    expect(!allRequiredReady(items), "no audio device is not ready");
}

void optionalMidiDoesNotBlockReady()
{
    auto in = allGood();
    in.midiConnected = false; in.midiDeviceName.clear();
    const auto items = buildFirstLaunchChecklist(in);
    expect(!items[3].ok, "midi row not ok when no keyboard");
    expect(allRequiredReady(items), "missing optional MIDI is still ready");
    expect(!items[3].detail.empty(), "midi row explains the on-screen keyboard");
}
}

int main()
{
    checklistHasFourItemsInOrder();
    allGoodIsReadyAndShowsDetails();
    missingSoundFontBlocksReady();
    nullSynthBlocksAudioEvenWithDevice();
    noAudioDeviceBlocksReady();
    optionalMidiDoesNotBlockReady();

    if (failures == 0)
        std::cout << "All FirstLaunchChecklist tests passed.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
