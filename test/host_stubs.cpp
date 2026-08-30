// Implementations for the host stand-ins, plus stand-ins for the storage layer
// (SD / NVS) so the UI can be driven without a card.
#include <M5Cardputer.h>
#include "storage/storage.h"
#include <cstdio>
#include <cstring>

M5Stub  M5;
EspStub ESP;
CardputerStub M5Cardputer;

namespace fonts { const lgfx::IFont Font0{0}, Font2{2}, Font4{4}; }

static uint32_t g_ms = 0;
uint32_t millis() { return g_ms += 7; }
uint32_t micros() { return g_ms * 1000; }
void delay(uint32_t) {}
uint32_t esp_random() { static uint32_t s = 12345; s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

// Walking the string is the whole point: a bad const char* faults here under
// ASan instead of silently rendering garbage (or rebooting) on the device.
extern "C" void __sanitizer_print_stack_trace(void);
void hostCheckString(const char* s) {
    if (!s) {
        fprintf(stderr, "FATAL: null string passed to the display\n");
        __sanitizer_print_stack_trace();
        abort();
    }
    volatile size_t n = strlen(s);
    (void)n;
}

const KeyValue_t _key_value_map[4][14] = {
    {{'`','~'},{'1','!'},{'2','@'},{'3','#'},{'4','$'},{'5','%'},{'6','^'},{'7','&'},
     {'8','*'},{'9','('},{'0',')'},{'-','_'},{'=','+'},{KEY_BACKSPACE,KEY_BACKSPACE}},
    {{KEY_TAB,KEY_TAB},{'q','Q'},{'w','W'},{'e','E'},{'r','R'},{'t','T'},{'y','Y'},{'u','U'},
     {'i','I'},{'o','O'},{'p','P'},{'[','{'},{']','}'},{'\\','|'}},
    {{KEY_FN,KEY_FN},{KEY_LEFT_SHIFT,KEY_LEFT_SHIFT},{'a','A'},{'s','S'},{'d','D'},{'f','F'},
     {'g','G'},{'h','H'},{'j','J'},{'k','K'},{'l','L'},{';',':'},{'\'','"'},{KEY_ENTER,KEY_ENTER}},
    {{KEY_LEFT_CTRL,KEY_LEFT_CTRL},{KEY_OPT,KEY_OPT},{KEY_LEFT_ALT,KEY_LEFT_ALT},{'z','Z'},
     {'x','X'},{'c','C'},{'v','V'},{'b','B'},{'n','N'},{'m','M'},{',','<'},{'.','>'},
     {'/','?'},{' ',' '}},
};

// --- storage stand-ins ------------------------------------------------------
namespace synth {
void settingsLoad(Settings&) {}
void settingsSave(const Settings&) {}
bool sdAvailable() { return false; }
bool projectSave(const Project&, const char*, char* err, int n) { snprintf(err, n, "No SD card."); return false; }
bool projectLoad(Project&, const char*, char* err, int n) { snprintf(err, n, "No SD card."); return false; }
bool projectDelete(const char*, char* err, int n) { snprintf(err, n, "No SD card."); return false; }
int  projectList(char out[][kNameLen], int maxCount) {
    // Pretend a card with a few projects so the FILE browser is exercised.
    const char* names[] = {"DEMO", "ACIDJAM", "A_VERY_LONG_NAME"};
    int n = 0;
    for (; n < 3 && n < maxCount; ++n) { strncpy(out[n], names[n], kNameLen - 1); out[n][kNameLen - 1] = 0; }
    return n;
}
}  // namespace synth
