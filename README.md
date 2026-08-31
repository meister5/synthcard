# SynthCard

A standalone pocket groovebox for the **M5Stack Cardputer ADV**. Synth, drum
machine, step sequencer, arranger and effects — no phone, no DAW, no MIDI host.
Turn it on, pick a sound, make a beat.

![license](https://img.shields.io/badge/license-MIT-blue) ![board](https://img.shields.io/badge/board-Cardputer%20ADV-orange)

```
 PLAY  ▶ 124 BPM  PTN 1   ▪▪▪▫▫▫▫▫▫▫▫▫▫▫▫▫
 ┌ LEAD ┐ 03/48                ╭──────────╮
  ACID LEAD                    │ ∿∿∿∿∿∿∿∿ │
  ANALOG  SAW  LPF             ╰──────────╯
  CUTOFF              41%       OCT 4  PENT MIN
  ██████████░░░░░░░░░░░         ARP UP 1/16
  RESO                88%       ▪▪▪▪▪▫▫▫
  ████████████████████░
  ████████████████░░░░░░░░░░░░░░░░░░░░░░░░
 Z..? PLAY  []=CUTOFF  OP=RESO  A=ARP   LEAD OCT4 ARP
```

...and hold `FN` when you forget what a key does:

```
 PLAY  ▶ 124 BPM  PTN 1   ▪▪▪▫▫▫▫▫▫▫▫▫▫▫▫▫
 ┌ HOLD FN ──────────────────── release to play ┐
 │ `   MENU        O   SAVE        ;   UP       │
 │ 1-7 GO TO MODE  P   LOAD        '   MUTE     │
 │ 9   BPM DOWN    [   VALUE DOWN  ,   LEFT     │
 │ 0   BPM UP      ]   VALUE UP    .   DOWN     │
 │ Q   RANDOM DRUMS \  SONG MODE   /   RIGHT    │
 │ W   RANDOM BASS  A   ARP MODE   SPACE TAP    │
 │ E   RANDOM LEAD  F   PREV SOUND              │
 └──────────────────────────────────────────────┘
```

---

## Hardware

Everything below was read out of the installed M5Unified / M5Cardputer sources
and the ESP32 board definition rather than assumed from the original Cardputer.

| | |
| --- | --- |
| Module | M5Stamp-S3A — **ESP32-S3FN8**, dual-core Xtensa LX7 @ 240 MHz, hardware FPU |
| RAM | 512 KB SRAM (~320 KB available to the sketch). **No PSRAM.** |
| Flash | 8 MB. Default partition scheme: two 1.25 MB OTA slots |
| Display | 240 × 135 ST7789, 16-bit colour, driven by M5GFX. *Identical to the original Cardputer* |
| Keyboard | **TCA8418** I²C matrix controller with an event FIFO, INT on GPIO 11 (the original Cardputer uses a 74HC138 scan instead) |
| Audio out | **ES8311 I²S codec** on I2S1 — BCK 41, WS 43, DATA 42. Needs the I²C register bring-up that `M5Unified` performs in `_speaker_enabled_cb_cardputer_adv`, so all audio must go through `M5.Speaker` |
| Microphone | ES8311 ADC, shares the codec with the speaker — **deliberately disabled** (`cfg.internal_mic = false`) |
| SD card | SPI — SCK 40, MOSI 14, MISO 39, CS 12 |
| Encoder | none — the ADV has no rotary control, so parameter editing is on `O`/`P` + `[`/`]` |
| Battery | read via `M5.Power.getBatteryLevel()` |
| Toolchain | Arduino, `m5stack:esp32` core 3.3.9, M5Unified 0.2.20 + M5Cardputer 1.1.1 |

There is no separate ADV board definition; M5GFX detects the ADV at runtime, so
one FQBN covers both machines. SynthCard refuses to run on a non-ADV board
rather than driving the wrong pins.

## Build

```bash
arduino-cli core install m5stack:esp32
arduino-cli lib install M5Unified M5Cardputer
tools/package.sh                 # -> dist/SynthCard.bin and dist/SynthCard-merged.bin
```

Host tests and tools. Everything below `app.cpp` except the Arduino-facing
files builds and runs on a desktop, which is what lets the DSP be measured
rather than guessed at.

```bash
make -C test all-tests      # ~830k assertions, and the UI under ASan + UBSan
make -C test render-wav     # renders kits, engines and a demo to test/out/
make -C test bench-run      # per-engine and per-lane cost
make -C test controls-md    # regenerates CONTROLS.md from the keymap
```

`all-tests` is two suites. The first covers the DSP, sequencer timing, music
theory and the file format — and, since this round, the sound itself: kick
level through a 200 Hz high-pass, lane balance against the kick, choke timing,
and per-engine tuning and off-harmonic energy. The second builds the *real*
`ui/`, `app` and `engine` sources against stand-ins for M5GFX and FreeRTOS and
drives every screen, action and key under AddressSanitizer, because a bad index
in the UI reboots the device rather than misdrawing. It also asserts that every
action has a label and that no drawing call ever asks for a colour outside the
16-entry palette — the two properties the live legend and the 4-bit canvas rest
on.

`render-wav` is the one that matters most. "Does the kick slap" is not
something an assertion can decide, so the renderer writes WAVs to listen to and
prints the measurements alongside them. Every sound bug fixed in this round was
found that way and is now pinned by a test.

## Flash

**M5Launcher / OTA** — point it at `SynthCard.bin` (this repo keeps a built
copy at the root so the raw GitHub URL always serves the current firmware).

**esptool / M5Burner** — flash the merged image at offset 0:

```bash
esptool --chip esp32s3 write_flash 0x0 dist/SynthCard-merged.bin
```

**arduino-cli** — `arduino-cli upload -p /dev/ttyACM0 --fqbn m5stack:esp32:m5stack_cardputer:PSRAM=disabled SynthCard`

## Controls

Full reference in [CONTROLS.md](CONTROLS.md), and on the device under
`FN`+`ENTER`. The short version:

- `` ` `` opens the manual **for the tab you are on** — every key, on the device
- `SPACE` play/stop, `\` record, hold `BKSP` to erase, `TAB` change mode
- `Z X C V B N M , . /` + `S D G H J L ;` = a 17-key piano; `-` / `=` octave
- `1`–`8` and `Q`–`I` = steps 1–16
- `O` / `P` select a parameter, `[` / `]` change it
- `FN` + arrows (`; . , /`) navigate; `FN`+`1`…`7` jumps between modes
- `SHIFT` is the pattern layer: `SHIFT`+`1`…`8` jumps, `SHIFT`+`[`/`]` steps,
  `SHIFT`+`O`/`P` sets pattern length
- `CTRL`+`Z` undoes (press again to redo)
- `FN`+`Q`/`W`/`E`/`R` generate a beat, bassline, melody or sound

**Nothing above has to be memorised.** Hold `FN` or `SHIFT` for a moment and
the screen lists everything that modifier does, right now, on the screen you
are on; let go and you are playing again. The list is generated by walking the
same function the firmware dispatches through, so it cannot describe a binding
that does not exist or miss one that does — and `CONTROLS.md` is generated from
the same tables, which is why it cannot go stale either.

## Modes

| # | Mode | What it is |
| --- | --- | --- |
| 1 | **PLAY** | Live keyboard, scope, cutoff/resonance under your fingers, drum pads on the number row. The "pulled it out of my pocket" screen. |
| 2 | **DRUM** | Twelve lanes × up to 64 steps, 16 direct step keys, three performance macros and eight parameters per lane, twelve kits. |
| 3 | **SEQ** | Piano-roll step editor for the LEAD and BASS tracks: note, velocity, gate, chord, probability, slide, per-step mute. |
| 4 | **SOUND** | The selected engine's parameter pages. The cursor wraps across pages, so paging is a shortcut rather than a step. |
| 5 | **FX** | Delay (free or tempo-synced), plate reverb, chorus, tilt, compressor, drive, master level. |
| 6 | **SONG** | Chain up to 64 pattern slots with repeat counts. |
| 7 | **SETUP** | Two tabs. FILES names, saves, loads and deletes projects on the card; SYSTEM holds volume, brightness, output mode, metronome, swing, key, chord mode, the arpeggiator and pattern length, with live CPU / RAM / FPS / battery. |

## The instrument

**Synth** — 8-voice polyphony across two independent tracks (LEAD and BASS),
each with its own patch. **Six engines**, each with its own controls:

- *ANALOG* — two anti-aliased (PolyBLEP) oscillators — saw, square, pulse with
  PWM, triangle, sine, noise — a sub oscillator, a noise source, and unison up
  to four with detune spread.
- *FM* — four operators, eight algorithms, musical ratios, a per-operator
  envelope and feedback on operator one.
- *WAVETBL* — sixteen band-limited tables with morph and phase-distortion warp.
  Every table is mip-mapped to six octave bands and synthesised additively with
  the harmonic sum truncated at each band's Nyquist, so it is alias-free by
  construction rather than by filtering afterwards.
- *PLUCK* — Karplus-Strong: an excitation shaped by brightness and pick
  position, a damped tuned delay, and a body resonator. Covers plucked strings,
  harps, mallets and bowed tones.
- *ORGAN* — nine additive drawbars at the Hammond footages, with key click and
  a rotary. Partials that would land above Nyquist are muted per note.
- *CHIP* — a real 15-bit LFSR noise channel, four fixed pulse widths, the fast
  per-note arpeggio that fakes chords on a mono channel, and delayed vibrato.

The engine's own controls live in a 20-slot overlay on the parameter array, so
six engines fit in 52 bytes and FM's operator knobs never appear on ORGAN.
Changing engine reloads only that block: your filter, envelopes and levels
survive, so switching is exploratory rather than destructive.

Shared by every engine: an amplitude ADSR, a second ADSR for the filter, a
zero-delay state-variable filter (LP / HP / BP / notch, 12 or 24 dB) with
resonance, key tracking, velocity depth and bipolar envelope depth, and an LFO
routable to pitch, filter, amplitude or wave shape. Poly, mono and legato voice
modes with portamento. **48 factory presets.**

**Drums** — Twelve fully synthesised voices — kick, snare, closed and open hat,
ride, crash, clap, tom, rim, cowbell, shaker, perc — across five synthesis
families, in twelve kits. Eight parameters per lane (tune, decay, tone, level,
snap, drive, delay send, reverb send) plus three performance macros (PUNCH,
TONE, SPACE) on the front page. Closed hat, open hat and ride cut each other
off. Velocity shapes the transient and the pitch envelope, not just the volume.
No samples, so nothing to load off the card.

The kick is built around a fact about this hardware: the Cardputer's speaker
has almost no output below ~200 Hz, so a kick whose weight lives in its
fundamental is inaudible on the device however well it is tuned. Its weight
comes instead from a two-stage pitch envelope sweeping through 350–150 Hz and
from saturation putting harmonics there, with a clean sub layer underneath for
headphones. The host renderer measures exactly that — peak level surviving a
200 Hz high-pass — for every kit.

**Sequencer** — Eight patterns, 1–64 steps each (resizable live from any
screen), fourteen tracks per pattern
(LEAD, BASS, and the twelve drum lanes). Per-step note, velocity, gate length, chord,
probability, slide and mute. Gates run from a sixteenth of a step up to
seventeen whole steps, so notes can sustain across the bar. Global swing,
BPM 40–300, tap tempo. Patterns
chain into a 64-slot song with repeats.

**Recording** — Arm with `\` and play: notes land on the nearest step of the
running pattern and keep looping. **How long you hold a key is recorded**, so a
sustained note plays back sustained. Hold `BKSP` while the loop passes to rub
notes out again. Switch tracks and keep layering. Nothing is destructive —
every recorded step is editable afterwards on the SEQ page, and `CTRL`+`Z`
undoes a whole take. A metronome (off / always / while recording only) gives
you something to play to when the pattern is still empty.

**Chords** — A step stores a root note plus a chord type (power, triad or
seventh) rather than fixed pitches, and the voicing is built at playback from
the song's scale and root. Chords therefore stay in key, and changing the key
reshapes every chord in the song instead of breaking it. Recording with chord
mode on stamps the type onto the step; a mono patch plays the root alone.

**Performance** — Ten scales (chromatic, major, minor, both pentatonics, blues,
dorian, mixolydian, harmonic minor, phrygian) that constrain the keyboard to
the chosen key; chord mode (power / triad / seventh, built from the current
scale); a five-mode arpeggiator with rate, octave range and gate.

**Generators** — Euclidean rhythms, random beats, basslines and melodies that
stay in key, and a patch randomiser that moves only the parameters that keep a
sound musical, within per-parameter ranges.

**Effects** — A delay that runs free or snaps to note divisions of the tempo, a
Dattorro-style plate reverb (four input diffusers into a modulated two-allpass
tank), chorus, a one-knob tilt EQ, a soft-knee compressor and a master peak
limiter. Every synth track and **every drum lane** has its own delay and reverb
send.

`SPEAKER` / `LINE` output modes matter more than they sound like they should.
On `SPEAKER` the master is high-passed at 120 Hz, because energy the speaker
cannot reproduce was otherwise still driving the limiter — a big kick ducking
the whole mix in exchange for something nobody can hear.

## Architecture

```
SynthCard/
  SynthCard.ino          setup()/loop() only
  src/
    app.{h,cpp}          state, key routing, global commands, menu
    audio/
      dsp.{h,cpp}        rate, fast sin/exp2, soft clip, PolyBLEP, RNG,
                         Env / SVF / LFO, denormal flushing
      patch.{h,cpp}      common params + per-engine overlay, pages,
                         48 presets, formatting
      voice.{h,cpp}      envelopes, filter, LFO, glide, output stage
      engines.h          the engine interface and the state union
      eng_analog.cpp     2 osc + sub + noise, unison
      eng_fm.cpp         4 operators, 8 algorithms, feedback
      eng_wt.cpp         16 mip-mapped tables, morph and warp
      eng_pluck.cpp      Karplus-Strong, pooled delay lines
      eng_organ.cpp      9 additive drawbars
      eng_chip.cpp       LFSR noise, fixed duties, per-note arp
      wavetables.{h,cpp} GENERATED band-limited tables (flash only)
      drum_voice.h       the shared drum voice and family interface
      drum_membrane.cpp  kick, tom
      drum_snare.cpp     snare, rim
      drum_metal.cpp     hats, ride, crash, cowbell
      drum_noise.cpp     clap, shaker
      drum_perc.cpp      the catch-all FM blip
      drums.{h,cpp}      twelve lanes, twelve kits, macros, choke groups
      effects.{h,cpp}    delay, plate reverb, chorus, tilt, comp, limiter
      engine.{h,cpp}     voice pool, buses, event queue, render task
    sequencer/
      sequencer.{h,cpp}  clock, patterns, song, live recording
    music/
      music.{h,cpp}      scales, arpeggiator, euclid, randomisers
    input/
      keys.{h,cpp}       FIFO draining, key edges, keymap, command layer
    ui/
      ui.{h,cpp}         palette, widgets, chrome, overlays, boot screen
      screens.cpp        the seven screens
      legend.cpp         the live legend, generated from the keymap
      tour.cpp           the first-run walkthrough
    storage/
      storage.{h,cpp}    SD projects, NVS settings
      serialize.cpp      versioned project format
  test/                  host tests and tools (no Arduino needed)
    unit_tests.cpp       DSP, sequencer, music theory, file format,
                         and the sound-quality regressions
    ui_tests.cpp         every screen/action/key under ASan + UBSan,
                         plus the label, legend, tour and palette checks
    render.cpp           renders kits, engines and a demo to out/*.wav
    bench.cpp            per-engine and per-lane cost
    gen_controls.cpp     writes CONTROLS.md from the binding tables
    wav.h                WAV writing, FFT and the audio measurements
    stub/                host stand-ins for M5GFX and FreeRTOS
  tools/package.sh       build + size check + merged image
  tools/gen_wavetables.py  regenerates the wavetable data
```

Everything below `app.cpp` except `engine.cpp`, `storage.cpp`, `keys.cpp` and
the UI is free of Arduino headers, which is what lets the test suite build and
run the real DSP and sequencer code on a host machine.

## Audio engine

- **32 kHz mono, 128-sample blocks (4 ms).** The ES8311 runs at its 48 kHz
  default and M5Unified box-resamples our blocks up to it — the same path the
  MP3 players on this board use, and one less clocking assumption to get wrong.
- A dedicated FreeRTOS task pinned to **core 0** renders blocks and hands them
  to `M5.Speaker.playRaw()` through a three-buffer rotation, waiting on
  `isPlaying(channel) > 1`. The UI owns core 1. There is no WiFi or Bluetooth.
- The I²S DMA ring is shrunk to 4 × 256 frames (~21 ms) and the speaker task is
  pinned to core 0 at priority 5, above the render task at 4.
- **Nothing in the render path allocates.** All buffers are fixed-size members.
- The UI never touches engine state: it posts into a 96-entry lock-free ring
  that the audio task drains at the top of each block. Note-ons therefore land
  within one block of the keypress.
- The sequencer and arpeggiator are advanced **in sample counts, not blocks**.
  Each block is split at every step and gate boundary, so step timing is
  sample-accurate instead of quantised to 4 ms.
- Control-rate work (filter coefficients, LFO, glide) runs once per 16 samples;
  envelopes and oscillators run per sample.
- Voice stealing prefers the quietest releasing voice, then the oldest.
- A block-time EMA drives graceful degradation: above 88 % the polyphony drops a
  voice at a time down to 3, and recovers below 50 %.
- Every voice is soft-clipped, and the master bus ends in a fast-attack /
  slow-release peak limiter, so nothing downstream can clip the DAC.

## Project format

Projects live on the SD card at `/synthcard/<NAME>.SCP` — **and nowhere else**.
The directory is created the first time you save and the card is untouched
during normal play; it is mounted for the transfer and unmounted immediately.
Device settings (volume, brightness, last project) live in NVS instead, so the
firmware works fine with no card inserted.

The format is an explicit little-endian field-by-field encoding — magic `SCPJ`,
a version, then tempo, key, all eight patterns, the song, both patches, the kit
and the effect settings — ending in an FNV-1a checksum. A project is 10,809
bytes. The reader is version-aware and still loads v2 files: the drum lanes are
remapped (v2's lane 4 was CLAP, which is lane 6 now, so copying by index would
silently turn every saved clap into a ride) and patches are converted parameter
by parameter. The test suite proves this against a hand-built v2 image rather
than by assertion. Loading validates the magic, the version, the checksum, the
length and every field range, so a truncated or corrupt file is reported, not
crashed on.

Save and load need a ~12 KB staging buffer, which used to be a static array
resident for the whole session in order to be used for a few milliseconds twice
a day. The caller now lends the undo buffer instead — the same size, and
expendable at exactly that moment, since loading a project clearing the undo
history is what anyone would expect. The format packs each step's gate and
chord into one byte, which is what keeps it inside a borrowed `Project`, and a
test asserts a fully populated project still fits.

Audio is suspended around every card access: the SPI transfer and FAT
bookkeeping are far too long for the render task.

## Performance notes

`make -C test bench` reports per-engine and per-lane cost. The absolute numbers
are meaningless — a desktop is not an LX7 — but the ratios are the point, and
so is watching them between commits. The six engines land between 1.0× and
2.3× an ANALOG voice; ORGAN and FM are the expensive ones, and eight voices of
either will lean on the adaptive voice-shedding, which is what it is for.

That benchmark earned its keep immediately: a sustained Karplus-Strong string
measured 18× an ANALOG voice. It is not — it is 1.2×. The difference was
denormals. After about nine seconds the loop's filter states decay past 1e-38,
and denormal arithmetic is handled in software. Filter and loop states are now
flushed once per block throughout the engine.

**Memory.** Static RAM is 105 KB, down from 122 KB before this work, despite
six engines instead of four and twelve drum lanes instead of nine. The canvas is a 4-bit palette sprite: 16.2 KB from the heap instead of
64.8 KB. The effects rack is 27 KB instead of 44 KB — the delay line runs at
half rate for the same 500 ms, and the better reverb is the same size as the
worse one. A step is four bytes rather than five - gate needs five bits and chord needs
two, so they share a byte - which is 2 KB across the project and its undo copy.
The firmware is ~702 KB, just over half of a 1.25 MB OTA slot; 16 KB of that is
wavetable data, which costs flash and no RAM at all.

Altogether that is about 66 KB returned to the heap and the static pool
compared with the firmware this replaced.

The UI is capped at ~33 fps so the display push never competes with audio.

## Known limitations

- **Mono output.** The Cardputer ADV has one speaker; there is no panning.
- **No microphone.** Mic and speaker share the ES8311, and the speaker wins.
- **No MIDI.** Deliberately out of scope for v1 — the standalone instrument came
  first, as the brief asked.
- **No sample playback.** Drums are synthesised so the card stays optional.
- **16 kHz of bandwidth.** 32 kHz internal rate is the CPU/quality trade; hats
  and crashes are a little softer than they would be at 48 kHz.
- **Fixed velocity.** The keyboard has no pressure sensing, so velocity comes
  from `SHIFT` (accent) and `ALT` (soft) instead.
- **Polyphony under the heavier engines.** ORGAN and FM cost roughly 2.3× an
  ANALOG voice, so eight simultaneous voices of either will trip the adaptive
  shedding down towards three. That is the designed behaviour rather than a
  dropout, but it is a real ceiling.
- **Unison spends polyphony.** A 4× supersaw patch gives two notes rather than
  eight, because unison is charged as voice slots instead of quietly asking the
  CPU for four times the work.
- **Not yet run on hardware by the author.** Everything here compiles for the
  ADV and is covered by ~830k host assertions, and the audio has been rendered
  and measured off-device — but it has not been heard through the real speaker.
  The two things most worth checking first are the frame rate (the palette
  canvas changes how pixels are written) and whether the kick lands.

## Licence

MIT — see [LICENSE](LICENSE).
