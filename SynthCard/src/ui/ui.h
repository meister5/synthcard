// SynthCard - display theme and drawing helpers.
#pragma once
#include <stdint.h>
#include <M5Unified.h>

namespace synth {
namespace ui {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr int W = 240, H = 135;
constexpr int TOP_H = 13, HINT_H = 15;
constexpr int BODY_Y = TOP_H + 1;
constexpr int BODY_H = H - TOP_H - HINT_H - 2;

constexpr uint16_t C_BG     = rgb(11, 14, 20);
constexpr uint16_t C_PANEL  = rgb(23, 28, 40);
constexpr uint16_t C_PANEL2 = rgb(34, 41, 56);
constexpr uint16_t C_TEXT   = rgb(232, 236, 242);
constexpr uint16_t C_DIM    = rgb(112, 122, 140);
constexpr uint16_t C_FAINT  = rgb(56, 64, 80);
constexpr uint16_t C_ACCENT = rgb(255, 162, 43);
constexpr uint16_t C_ACC2   = rgb(53, 214, 208);
constexpr uint16_t C_REC    = rgb(255, 75, 75);
constexpr uint16_t C_OK     = rgb(92, 224, 138);
constexpr uint16_t C_GRID   = rgb(42, 49, 64);

// Horizontal value bar with a hairline track.
void bar(M5Canvas& g, int x, int y, int w, int h, float frac, uint16_t fg, uint16_t bg);
// Bipolar bar drawn out from the centre.
void barBipolar(M5Canvas& g, int x, int y, int w, int h, float v, uint16_t fg, uint16_t bg);
void panel(M5Canvas& g, int x, int y, int w, int h, uint16_t fill, uint16_t border);
void textAt(M5Canvas& g, int x, int y, const char* s, uint16_t col, const void* font, uint8_t datum = 0);

}  // namespace ui

M5Canvas& uiCanvas();
}  // namespace synth
