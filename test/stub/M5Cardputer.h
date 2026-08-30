#pragma once
#include <M5Unified.h>
#include <vector>

#define KEY_BACKSPACE 0x2a
#define KEY_TAB       0x2b
#define KEY_ENTER     0x28
#define KEY_FN        0xff
#define KEY_OPT       0x00
#define KEY_LEFT_CTRL  0x80
#define KEY_LEFT_SHIFT 0x81
#define KEY_LEFT_ALT   0x82

struct KeyValue_t { const char value_first, value_second; };
extern const KeyValue_t _key_value_map[4][14];

class KeyboardStub {
public:
    void begin() {}
    void updateKeyList() {}
    void updateKeysState() {}
    const std::vector<Point2D_t>& keyList() { return held; }
    std::vector<Point2D_t> held;
};

class CardputerStub {
public:
    void begin(bool = true) {}
    void begin(M5Stub::Config, bool = true) {}
    void update() {}
    KeyboardStub Keyboard;
    M5GFX& Display = M5.Display;
};
extern CardputerStub M5Cardputer;
