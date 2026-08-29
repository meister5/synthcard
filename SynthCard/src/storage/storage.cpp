#include "storage.h"
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>

namespace synth {

// Cardputer ADV SD pins (from the M5Unified board table).
static constexpr int kSdSck = 40, kSdMosi = 14, kSdMiso = 39, kSdCs = 12;
static const char* kDir = "/synthcard";

static bool s_mounted = false;

static bool mount() {
    if (s_mounted) return true;
    SPI.end();
    SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (SD.begin(kSdCs, SPI, 20000000)) { s_mounted = true; return true; }
        delay(60);
    }
    return false;
}

static void unmount() {
    if (!s_mounted) return;
    SD.end();
    s_mounted = false;
}

bool sdAvailable() {
    bool ok = mount();
    unmount();
    return ok;
}

static void sanitize(const char* in, char* out, int cap) {
    int j = 0;
    for (int i = 0; in[i] && j < cap - 1; ++i) {
        char c = in[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        out[j++] = ok ? c : '_';
    }
    if (j == 0) out[j++] = 'X';
    out[j] = 0;
}

static uint8_t s_buf[kProjectBufSize];

bool projectSave(const Project& p, const char* name, char* err, int errLen) {
    char clean[kNameLen];
    sanitize(name, clean, kNameLen);
    int n = projectSerialize(p, s_buf, kProjectBufSize);
    if (n <= 0) { snprintf(err, errLen, "Project too large."); return false; }
    if (!mount()) { snprintf(err, errLen, "No SD card."); return false; }

    if (!SD.exists(kDir) && !SD.mkdir(kDir)) {
        unmount(); snprintf(err, errLen, "Cannot create %s", kDir); return false;
    }
    char path[64];
    snprintf(path, sizeof(path), "%s/%s.SCP", kDir, clean);
    File f = SD.open(path, FILE_WRITE);
    if (!f) { unmount(); snprintf(err, errLen, "Cannot open file."); return false; }
    size_t wrote = f.write(s_buf, (size_t)n);
    f.flush();
    f.close();
    unmount();
    if (wrote != (size_t)n) { snprintf(err, errLen, "Write failed - card full?"); return false; }
    err[0] = 0;
    return true;
}

bool projectLoad(Project& p, const char* name, char* err, int errLen) {
    char clean[kNameLen];
    sanitize(name, clean, kNameLen);
    if (!mount()) { snprintf(err, errLen, "No SD card."); return false; }
    char path[64];
    snprintf(path, sizeof(path), "%s/%s.SCP", kDir, clean);
    File f = SD.open(path, FILE_READ);
    if (!f) { unmount(); snprintf(err, errLen, "Not found: %s", clean); return false; }
    size_t sz = f.size();
    if (sz == 0 || sz > (size_t)kProjectBufSize) {
        f.close(); unmount(); snprintf(err, errLen, "Bad file size."); return false;
    }
    size_t got = f.read(s_buf, sz);
    f.close();
    unmount();
    if (got != sz) { snprintf(err, errLen, "Read failed."); return false; }
    if (!projectDeserialize(p, s_buf, (int)sz)) { snprintf(err, errLen, "Corrupt project."); return false; }
    err[0] = 0;
    return true;
}

bool projectDelete(const char* name, char* err, int errLen) {
    char clean[kNameLen];
    sanitize(name, clean, kNameLen);
    if (!mount()) { snprintf(err, errLen, "No SD card."); return false; }
    char path[64];
    snprintf(path, sizeof(path), "%s/%s.SCP", kDir, clean);
    bool ok = SD.remove(path);
    unmount();
    if (!ok) { snprintf(err, errLen, "Delete failed."); return false; }
    err[0] = 0;
    return true;
}

int projectList(char out[][kNameLen], int maxCount) {
    if (!mount()) return 0;
    int n = 0;
    File dir = SD.open(kDir);
    if (dir && dir.isDirectory()) {
        for (File e = dir.openNextFile(); e && n < maxCount; e = dir.openNextFile()) {
            const char* fn = e.name();
            const char* slash = strrchr(fn, '/');
            if (slash) fn = slash + 1;
            int len = (int)strlen(fn);
            if (!e.isDirectory() && len > 4 && strcasecmp(fn + len - 4, ".SCP") == 0) {
                int copy = len - 4;
                if (copy > kNameLen - 1) copy = kNameLen - 1;
                memcpy(out[n], fn, copy);
                out[n][copy] = 0;
                ++n;
            }
            e.close();
        }
    }
    if (dir) dir.close();
    unmount();
    return n;
}

// ---------------------------------------------------------------- settings --
static const char* kNvsNamespace = "synthcard";

void settingsLoad(Settings& s) {
    Preferences pref;
    if (!pref.begin(kNvsNamespace, true)) return;
    s.volume     = (uint8_t)pref.getUChar("vol", s.volume);
    s.brightness = (uint8_t)pref.getUChar("bri", s.brightness);
    String last  = pref.getString("last", "");
    strncpy(s.lastProject, last.c_str(), kNameLen - 1);
    s.lastProject[kNameLen - 1] = 0;
    pref.end();
}

void settingsSave(const Settings& s) {
    Preferences pref;
    if (!pref.begin(kNvsNamespace, false)) return;
    pref.putUChar("vol", s.volume);
    pref.putUChar("bri", s.brightness);
    pref.putString("last", s.lastProject);
    pref.end();
}

}  // namespace synth
