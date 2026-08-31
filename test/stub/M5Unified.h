// Host-side stand-in for M5Unified / M5GFX, just enough surface for the UI,
// the engine and the app to compile and run off-device under a sanitizer.
// Drawing is clipped and discarded; the point is to execute the real indexing
// and string handling in ui.cpp / screens.cpp, not to render anything.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <string>

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
uint32_t esp_random();

namespace lgfx { struct IFont { int id; }; }
namespace fonts { extern const lgfx::IFont Font0, Font2, Font4; }

enum textdatum_t : uint8_t { top_left = 0, top_center, top_right };

static constexpr uint16_t TFT_BLACK = 0x0000;
static constexpr uint16_t TFT_RED   = 0xF800;

namespace m5 {
enum class board_t : uint8_t { board_unknown = 0, board_M5Cardputer, board_M5CardputerADV };
struct Point2D_t { int16_t x, y; bool operator==(const Point2D_t& o) const { return x == o.x && y == o.y; } };
}
using m5::Point2D_t;

// Every string that reaches the display is walked here, so a null or dangling
// const char* faults in the harness exactly as it would on the device.
void hostCheckString(const char* s);

class M5GFX {
public:
    void setRotation(int) {}
    void setBrightness(uint8_t) {}
    void fillScreen(uint16_t) {}
    // The display is 16-bit whatever depth the sprite runs at, so its colours
    // are packed values, not palette indices, and are not checked.
    void setTextColor(uint32_t) {}
    void setTextSize(int) {}
    void setCursor(int, int) {}
    void setFont(const lgfx::IFont*) {}
    void print(const char* s) { hostCheckString(s); }
    int  width() const { return 240; }
    int  height() const { return 135; }
};

// Records every colour the UI asks for. In palette mode the firmware must
// never request an index outside the palette, and hostCheckColor makes that a
// test failure rather than a wrong-coloured pixel nobody notices.
void hostCheckColor(uint32_t c);

class M5Canvas {
public:
    explicit M5Canvas(M5GFX* = nullptr) {}
    void setColorDepth(int d) { depth_ = d; }
    void setPaletteColor(int i, uint8_t, uint8_t, uint8_t) { if (i > maxPal_) maxPal_ = i; }
    void* createSprite(int w, int h) { w_ = w; h_ = h; buf_ = new uint8_t[(size_t)w * h * 2]; return buf_; }
    void setTextWrap(bool) {}
    void fillSprite(uint32_t c) { hostCheckColor(c); }
    void pushSprite(int, int) {}
    void fillRect(int,int,int,int,uint32_t c) { hostCheckColor(c); }
    void drawRect(int,int,int,int,uint32_t c) { hostCheckColor(c); }
    void fillRoundRect(int,int,int,int,int,uint32_t c) { hostCheckColor(c); }
    void drawRoundRect(int,int,int,int,int,uint32_t c) { hostCheckColor(c); }
    void drawFastHLine(int,int,int,uint32_t c) { hostCheckColor(c); }
    void drawFastVLine(int,int,int,uint32_t c) { hostCheckColor(c); }
    void fillTriangle(int,int,int,int,int,int,uint32_t c) { hostCheckColor(c); }
    void fillCircle(int,int,int,uint32_t c) { hostCheckColor(c); }
    void drawCircle(int,int,int,uint32_t c) { hostCheckColor(c); }
    void drawLine(int,int,int,int,uint32_t c) { hostCheckColor(c); }
    void drawPixel(int,int,uint32_t c) { hostCheckColor(c); }
    void setFont(const lgfx::IFont*) {}
    void setTextColor(uint32_t c) { hostCheckColor(c); }
    void setTextDatum(uint8_t) {}
    void drawString(const char* s, int, int) { hostCheckString(s); }
    ~M5Canvas() { delete[] buf_; }
private:
    int depth_ = 16, w_ = 0, h_ = 0, maxPal_ = -1;
    uint8_t* buf_ = nullptr;
};

struct speaker_config_t {
    size_t dma_buf_len = 256, dma_buf_count = 8;
    uint8_t task_priority = 2, task_pinned_core = 255;
};
class SpeakerStub {
public:
    bool isEnabled() const { return true; }
    speaker_config_t config() const { return cfg_; }
    void config(const speaker_config_t& c) { cfg_ = c; }
    bool begin() { return true; }
    void setVolume(uint8_t v) { vol_ = v; }
    uint8_t getVolume() const { return vol_; }
    void setChannelVolume(uint8_t, uint8_t) {}
    size_t isPlaying(uint8_t) const { return 0; }
    bool playRaw(const int16_t*, size_t, uint32_t, bool, uint32_t, int, bool) { return true; }
    void stop(uint8_t) {}
private:
    speaker_config_t cfg_;
    uint8_t vol_ = 190;
};
class PowerStub { public: int getBatteryLevel() { return 77; } };

class M5Stub {
public:
    struct Config { bool internal_spk = true, internal_mic = true, output_power = true; };
    Config config() { return Config(); }
    void begin() {}
    void begin(Config) {}
    void update() {}
    m5::board_t getBoard() const { return m5::board_t::board_M5CardputerADV; }
    M5GFX Display;
    SpeakerStub Speaker;
    PowerStub Power;
};
extern M5Stub M5;

struct EspStub { uint32_t getFreeHeap() { return 160000; } };
extern EspStub ESP;
