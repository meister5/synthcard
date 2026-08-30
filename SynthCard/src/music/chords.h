// SynthCard - chord types, shared by the live keyboard and the sequencer.
//
// Kept in its own header because sequencer.h needs the enum while music.h
// needs Pattern, and one of those includes has to be the thin one.
#pragma once
#include <stdint.h>

namespace synth {

enum ChordType : uint8_t { CHORD_OFF = 0, CHORD_POWER, CHORD_TRIAD, CHORD_SEVENTH, CHORD_COUNT };
extern const char* const kChordNames[CHORD_COUNT];

// Expands `note` into up to 4 notes of the given chord type. Outside the
// chromatic scale the thirds come from the scale itself, so a chord stays in
// key and survives transposition. Returns how many notes were written.
int buildChord(uint8_t note, uint8_t type, uint8_t root, uint8_t scaleIdx, uint8_t out[4]);

}  // namespace synth
