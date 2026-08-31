// SynthCard - display theme and drawing helpers.
#pragma once
#include <stdint.h>
#include <M5Unified.h>

namespace synth {
namespace ui {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// A full-screen 16-bit sprite is 64.8 KB - by a wide margin the largest single
// allocation in the firmware. The interface uses twelve colours and draws no
// gradients, so it fits a 16-entry palette and a 4-bit sprite: 16.2 KB, for a
// saving of 48.6 KB.
//
// Set this to 0 to fall back to the 16-bit sprite if the palette ever looks
// wrong on real hardware. The host tests assert that nothing ever asks for a
// colour outside the palette, so the fallback should stay unnecessary.
#ifndef SC_CANVAS_4BIT
#define SC_CANVAS_4BIT 1
#endif

struct Rgb { uint8_t r, g, b; };

// The entire interface, in sixteen colours. Index order is the C_* order below
// and both modes read the same table, so the two builds cannot drift apart.
constexpr Rgb kPalette[16] = {
    { 11,  14,  20},   //  0 background
    { 23,  28,  40},   //  1 panel
    { 34,  41,  56},   //  2 panel, raised
    {232, 236, 242},   //  3 text
    {112, 122, 140},   //  4 dim text
    { 56,  64,  80},   //  5 faint
    {255, 162,  43},   //  6 accent
    { 53, 214, 208},   //  7 accent 2
    {255,  75,  75},   //  8 record
    { 92, 224, 138},   //  9 ok
    { 42,  49,  64},   // 10 grid
    { 70,  20,  26},   // 11 record top bar
    {  0,   0,   0},   // 12 black
    {255, 255, 255},   // 13 white
    { 24,  30,  22},   // 14 spare
    { 30,  22,  24},   // 15 spare
};
constexpr int kPaletteSize = (int)(sizeof(kPalette) / sizeof(kPalette[0]));

#if SC_CANVAS_4BIT
// In a palette sprite the drawing calls take an index, not a packed colour.
using Color = uint8_t;
constexpr Color paletteColor(int i) { return (Color)i; }
#else
using Color = uint16_t;
constexpr Color paletteColor(int i) { return rgb(kPalette[i].r, kPalette[i].g, kPalette[i].b); }
#endif

// The display itself is always 16-bit, whatever depth the sprite runs at, so
// the handful of calls that draw straight to it need a packed colour rather
// than a palette index.
constexpr uint16_t displayRgb(int i) { return rgb(kPalette[i].r, kPalette[i].g, kPalette[i].b); }

constexpr int W = 240, H = 135;
constexpr int TOP_H = 13, HINT_H = 15;
constexpr int BODY_Y = TOP_H + 1;
constexpr int BODY_H = H - TOP_H - HINT_H - 2;

constexpr Color C_BG     = paletteColor(0);
constexpr Color C_PANEL  = paletteColor(1);
constexpr Color C_PANEL2 = paletteColor(2);
constexpr Color C_TEXT   = paletteColor(3);
constexpr Color C_DIM    = paletteColor(4);
constexpr Color C_FAINT  = paletteColor(5);
constexpr Color C_ACCENT = paletteColor(6);
constexpr Color C_ACC2   = paletteColor(7);
constexpr Color C_REC    = paletteColor(8);
constexpr Color C_OK     = paletteColor(9);
constexpr Color C_GRID   = paletteColor(10);
constexpr Color C_RECBAR = paletteColor(11);

// Horizontal value bar with a hairline track.
void bar(M5Canvas& g, int x, int y, int w, int h, float frac, Color fg, Color bg);
// Bipolar bar drawn out from the centre.
void barBipolar(M5Canvas& g, int x, int y, int w, int h, float v, Color fg, Color bg);
void panel(M5Canvas& g, int x, int y, int w, int h, Color fill, Color border);
void textAt(M5Canvas& g, int x, int y, const char* s, Color col, const void* font, uint8_t datum = 0);

}  // namespace ui

M5Canvas& uiCanvas();
}  // namespace synth
