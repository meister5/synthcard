#include "engine.h"
#include <M5Unified.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace synth {

static constexpr int      kSpkChannel = 0;
static constexpr uint32_t kOutRate    = (uint32_t)kSampleRate;

bool AudioEngine::begin(Project* proj) {
    proj_ = proj;
    for (int i = 0; i < kMaxVoices; ++i) {
        voices_[i].init(0x9E3779B9u + i * 0x85EBCA6Bu);
        voices_[i].setPatch(&proj_->patch[0]);
        vTrack_[i] = 0; vNote_[i] = 0;
    }
    drums_.init();
    drums_.setKit(&proj_->kit);
    fx_.init();
    fx_.applySettings(proj_->fx);
    seq_.init(proj_, this);
    arp_.init(this);
    qHead_ = qTail_ = 0;
    return true;
}

void AudioEngine::post(uint8_t type, uint8_t a, uint8_t b, uint8_t c) {
    uint16_t head = qHead_;
    uint16_t next = (uint16_t)((head + 1) % kQueueSize);
    if (next == qTail_) return;                 // full: drop rather than block
    queue_[head].type = type; queue_[head].a = a; queue_[head].b = b; queue_[head].c = c;
    qHead_ = next;
}

void AudioEngine::drainEvents() {
    while (qTail_ != qHead_) {
        Event e = { queue_[qTail_].type, queue_[qTail_].a, queue_[qTail_].b, queue_[qTail_].c };
        qTail_ = (uint16_t)((qTail_ + 1) % kQueueSize);
        handle(e);
    }
}

void AudioEngine::handle(const Event& e) {
    switch (e.type) {
        case EV_NOTE_ON: {
            const uint8_t tr = (uint8_t)(e.a & 0x7F);
            if (arp_.enabled() && tr == liveTrack_) arp_.noteOn(e.b, e.c);
            else startNote(tr, e.b, e.c, false);
            if (!(e.a & kNoRecord) && seq_.recording() && seq_.playing())
                seq_.recordNote(tr, e.b, e.c);
            break;
        }
        case EV_NOTE_OFF: {
            const uint8_t tr = (uint8_t)(e.a & 0x7F);
            if (arp_.enabled() && tr == liveTrack_) arp_.noteOff(e.b);
            else stopNote(tr, e.b);
            if (!(e.a & kNoRecord) && seq_.recording()) seq_.recordNoteOff(tr, e.b);
            break;
        }
        case EV_DRUM:
            drums_.trigger(e.a, e.b);
            if (seq_.recording() && seq_.playing()) seq_.recordDrum(e.a, e.b);
            break;
        case EV_ALL_OFF: seq_.allNotesOff(); arp_.reset(); killAll(); break;
        case EV_PANIC:   seq_.stop(); arp_.reset(); killAll(); drums_.allOff(); break;
        case EV_PLAY:    seq_.play(); break;
        case EV_STOP:    seq_.stop(); killAll(); break;
        case EV_TOGGLE:  if (seq_.playing()) { seq_.stop(); killAll(); } else seq_.play(); break;
        case EV_REC:     seq_.setRecording(e.a != 0); break;
        case EV_PATTERN: seq_.selectPattern(e.a, e.b != 0); break;
        case EV_SONGMODE:seq_.setSongMode(e.a != 0); break;
        case EV_ARP_ON:  arp_.setEnabled(e.a != 0); if (!e.a) killAll(); break;
        case EV_ERASE_STEP: seq_.eraseStep(e.a); break;
        default: break;
    }
}

// --------------------------------------------------------------- voices ----
int AudioEngine::allocVoice(uint8_t track, uint8_t note) {
    const Patch& pt = proj_->patch[track];
    uint8_t mode = pt.get(P_VOICE_MODE);

    if (mode != 0) {                               // mono / legato: one voice per track
        for (int i = 0; i < maxVoices_; ++i)
            if (voices_[i].active() && vTrack_[i] == track) return i;
    }
    // Same note already sounding on this track -> retrigger it.
    for (int i = 0; i < maxVoices_; ++i)
        if (voices_[i].active() && vTrack_[i] == track && vNote_[i] == note) return i;
    // Free voice.
    for (int i = 0; i < maxVoices_; ++i) if (!voices_[i].active()) return i;
    // Steal: quietest released voice, else oldest.
    int best = 0; float bestLevel = 1e9f; bool foundRel = false;
    for (int i = 0; i < maxVoices_; ++i) {
        if (!voices_[i].released()) continue;
        foundRel = true;
        if (voices_[i].envLevel() < bestLevel) { bestLevel = voices_[i].envLevel(); best = i; }
    }
    if (foundRel) return best;
    uint32_t oldest = 0xFFFFFFFFu;
    for (int i = 0; i < maxVoices_; ++i)
        if (voices_[i].age() < oldest) { oldest = voices_[i].age(); best = i; }
    return best;
}

void AudioEngine::startNote(uint8_t track, uint8_t note, uint8_t vel, bool slide) {
    if (track >= kMelTracks || note == 0 || note > 127) return;
    int i = allocVoice(track, note);
    const Patch& pt = proj_->patch[track];
    bool legato = slide || (pt.get(P_VOICE_MODE) == 2 && voices_[i].active() && vTrack_[i] == track);
    voices_[i].setPatch(&pt);
    voices_[i].noteOn(note, vel, legato);
    voices_[i].touch(++ageCounter_);
    vTrack_[i] = track;
    vNote_[i]  = note;
}

void AudioEngine::stopNote(uint8_t track, uint8_t note) {
    for (int i = 0; i < kMaxVoices; ++i)
        if (voices_[i].active() && vTrack_[i] == track && vNote_[i] == note) voices_[i].noteOff();
}

void AudioEngine::killAll() { for (auto& v : voices_) v.kill(); }

void AudioEngine::seqNoteOn(uint8_t track, uint8_t note, uint8_t vel, bool slide) { startNote(track, note, vel, slide); }
void AudioEngine::seqNoteOff(uint8_t track, uint8_t note) { stopNote(track, note); }
void AudioEngine::seqDrum(uint8_t lane, uint8_t vel) { drums_.trigger(lane, vel); }
void AudioEngine::arpNoteOn(uint8_t note, uint8_t vel) { startNote(liveTrack_, note, vel, false); }
void AudioEngine::arpNoteOff(uint8_t note) { stopNote(liveTrack_, note); }

// ---------------------------------------------------------------- render ---
void AudioEngine::renderSegment(int offset, int n) {
    for (int i = 0; i < kMaxVoices; ++i) {
        if (!voices_[i].active()) continue;
        uint8_t t = vTrack_[i] < kMelTracks ? vTrack_[i] : 0;
        voices_[i].render(busMel_[t] + offset, n);
    }
    drums_.render(busDrum_ + offset, n);
}

void AudioEngine::adaptLoad(uint32_t us, int samples) {
    float budget = (float)samples * 1000000.0f / kSampleRate;
    float load = (float)us / budget;
    cpuLoad_ += (load - cpuLoad_) * 0.1f;
    // Graceful degradation: shed voices before we start dropping blocks.
    if (cpuLoad_ > 0.88f && maxVoices_ > 3) { --maxVoices_; cpuLoad_ = 0.7f; }
    else if (cpuLoad_ < 0.50f && maxVoices_ < kMaxVoices) { ++maxVoices_; cpuLoad_ = 0.6f; }
}

void AudioEngine::renderBlock(int16_t* out, int n) {
    const uint32_t t0 = micros();
    drainEvents();

    // Control-rate refresh: cheap enough to just do every block, which means
    // BPM / patch / FX edits from the UI take effect within 4 ms.
    fx_.applySettings(proj_->fx);
    seq_.refreshTiming();
    arp_.configure(proj_->arpMode, proj_->arpRate, proj_->arpOct, proj_->arpGate, proj_->bpm);
    if ((proj_->arpOn != 0) != arp_.enabled()) arp_.setEnabled(proj_->arpOn != 0);

    for (int t = 0; t < kMelTracks; ++t) memset(busMel_[t], 0, sizeof(float) * n);
    memset(busDrum_, 0, sizeof(float) * n);

    int done = 0;
    while (done < n) {
        int seg = seq_.samplesUntilNext();
        int a   = arp_.samplesUntilNext();
        if (a < seg) seg = a;
        if (seg > n - done) seg = n - done;
        if (seg < 1) seg = 1;
        renderSegment(done, seg);
        seq_.advance(seg);
        arp_.advance(seg);
        done += seg;
    }

    const float sd0 = proj_->patch[0].norm(P_SEND_DLY), sr0 = proj_->patch[0].norm(P_SEND_REV);
    const float sd1 = proj_->patch[1].norm(P_SEND_DLY), sr1 = proj_->patch[1].norm(P_SEND_REV);
    const float sdD = proj_->kit.sendDly * (1.0f / 127.0f), srD = proj_->kit.sendRev * (1.0f / 127.0f);
    for (int i = 0; i < n; ++i) {
        float a = busMel_[0][i], b = busMel_[1][i], d = busDrum_[i];
        mix_[i]     = a + b + d;
        sendDly_[i] = a * sd0 + b * sd1 + d * sdD;
        sendRev_[i] = a * sr0 + b * sr1 + d * srD;
    }
    fx_.process(mix_, sendDly_, sendRev_, n);

    for (int i = 0; i < n; ++i) {
        float s = mix_[i];
        out[i] = (int16_t)(clampf(s, -1.0f, 1.0f) * 32000.0f);
        if (--scopeSkip_ <= 0) {
            scopeSkip_ = 4;
            scope_[scopePos_] = (int8_t)(clampf(s, -1.0f, 1.0f) * 120.0f);
            scopePos_ = (scopePos_ + 1) % kScopeSize;
        }
    }

    int act = 0;
    for (int i = 0; i < kMaxVoices; ++i) if (voices_[i].active()) ++act;
    activeVoices_ = act;
    adaptLoad(micros() - t0, n);
}

// ---------------------------------------------------- speaker + task -------
static AudioEngine* s_engine = nullptr;
static TaskHandle_t s_task = nullptr;
static volatile bool s_suspend = false;
static volatile bool s_idle = false;
static int16_t s_buf[3][kBlockSize];
static uint8_t s_volume = 190;

static void audioTask(void*) {
    int idx = 0;
    for (;;) {
        if (s_suspend) {
            if (!s_idle) { M5.Speaker.stop(kSpkChannel); s_idle = true; }
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        s_idle = false;
        // isPlaying() == 2 means both queue slots are full; wait for room.
        int guard = 0;
        while (M5.Speaker.isPlaying(kSpkChannel) > 1 && ++guard < 200) vTaskDelay(1);
        s_engine->renderBlock(s_buf[idx], kBlockSize);
        M5.Speaker.playRaw(s_buf[idx], kBlockSize, kOutRate, false, 1, kSpkChannel, false);
        idx = (idx + 1) % 3;
    }
}

bool audioStart(AudioEngine* engine) {
    s_engine = engine;
    if (!M5.Speaker.isEnabled()) return false;
    // M5.begin() already picked the ADV pin set and installed the ES8311
    // bring-up callback; we only shrink the DMA ring for lower latency and
    // pin the I2S task to the audio core.
    auto cfg = M5.Speaker.config();
    cfg.dma_buf_len      = 256;
    cfg.dma_buf_count    = 4;
    cfg.task_priority    = 5;
    cfg.task_pinned_core = 0;
    M5.Speaker.config(cfg);
    if (!M5.Speaker.begin()) return false;
    M5.Speaker.setVolume(s_volume);
    M5.Speaker.setChannelVolume(kSpkChannel, 255);
    // Core 0 is otherwise idle (no WiFi/BT); the UI owns core 1.
    return xTaskCreatePinnedToCore(audioTask, "scAudio", 4096, nullptr, 4, &s_task, 0) == pdPASS;
}

void audioSetVolume(uint8_t v) { s_volume = v; M5.Speaker.setVolume(v); }
uint8_t audioGetVolume() { return s_volume; }

// Parks the render task so SD/SPI work cannot stall it. Voices and the clock
// keep their state, so playback resumes exactly where it paused.
void AudioEngine::suspendAudio() {
    s_suspend = true;
    for (int i = 0; i < 200 && !s_idle; ++i) delay(2);
}
void AudioEngine::resumeAudio() { s_suspend = false; }

}  // namespace synth
