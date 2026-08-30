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
    char    lastProject[kNameLen] = {0};
};

void settingsLoad(Settings& s);
void settingsSave(const Settings& s);

// All of these mount and unmount the card themselves. Suspend audio first -
// the SPI transfer and FAT bookkeeping are far too long for the render task.
bool sdAvailable();
bool projectSave(const Project& p, const char* name, char* err, int errLen);
bool projectLoad(Project& p, const char* name, char* err, int errLen);
bool projectDelete(const char* name, char* err, int errLen);
int  projectList(char out[][kNameLen], int maxCount);

// Serialisation is split out so it can be exercised by the host unit tests.
constexpr uint32_t kProjectMagic   = 0x4A504353u;   // 'SCPJ'
constexpr uint16_t kProjectVersion = 1;
int  projectSerialize(const Project& p, uint8_t* buf, int cap);
bool projectDeserialize(Project& p, const uint8_t* buf, int len);
constexpr int kProjectBufSize = 12288;

}  // namespace synth
