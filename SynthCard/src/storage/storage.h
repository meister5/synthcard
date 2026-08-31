// SynthCard - project files (SD) and device settings (NVS).
//
// Projects live on the SD card under /synthcard/, and the directory is only
// created when the user explicitly saves - nothing is written to the card
// during normal play. Device settings live in NVS so the firmware still works
// with no card inserted.
#pragma once
#include <stdint.h>
#include "../sequencer/sequencer.h"

namespace synth {

constexpr int kMaxProjectFiles = 32;
constexpr int kNameLen         = 17;

struct Settings {
    uint8_t volume     = 190;
    uint8_t brightness = 80;
    uint8_t metronome  = 0;      // MetroMode
    uint8_t outMode    = 0;      // OutMode: SPEAKER conditions the master bus
    uint8_t tourDone   = 0;      // the first-run walkthrough has been seen
    char    lastProject[kNameLen] = {0};
};

void settingsLoad(Settings& s);
void settingsSave(const Settings& s);

// All of these mount and unmount the card themselves. Suspend audio first -
// the SPI transfer and FAT bookkeeping are far too long for the render task.
//
// Save and load need a ~12 KB staging buffer. It used to be a static array
// that sat resident for the whole session to be used for a few milliseconds
// twice a day; the caller now lends the undo buffer instead, which is the
// same size and is expendable at exactly this moment (loading a project
// clearing the undo history is what a user expects anyway).
bool sdAvailable();
bool projectSave(const Project& p, const char* name, void* scratch, char* err, int errLen);
bool projectLoad(Project& p, const char* name, void* scratch, char* err, int errLen);
bool projectDelete(const char* name, char* err, int errLen);
int  projectList(char out[][kNameLen], int maxCount);

// Serialisation is split out so it can be exercised by the host unit tests.
constexpr uint32_t kProjectMagic   = 0x4A504353u;   // 'SCPJ'
// v2 added Step::chord. v3 added the engine-overlay patch model, three drum
// lanes, four drum parameters and the per-lane macros; it still loads v2.
constexpr uint16_t kProjectVersion = 3;
int  projectSerialize(const Project& p, uint8_t* buf, int cap);
bool projectDeserialize(Project& p, const uint8_t* buf, int len);

// The staging buffer is a borrowed Project, so the format must always fit
// inside one. unit_tests.cpp asserts a fully populated project serialises
// within this, which is what makes the borrow safe rather than merely likely.
constexpr int kProjectBufSize = (int)sizeof(Project);

}  // namespace synth
