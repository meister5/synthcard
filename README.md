# SynthCard

A standalone pocket groovebox for the **M5Stack Cardputer ADV**. Synth, drum
machine, step sequencer, arranger and effects — no phone, no DAW, no MIDI host.
Turn it on, pick a sound, make a beat.

![license](https://img.shields.io/badge/license-MIT-blue) ![board](https://img.shields.io/badge/board-Cardputer%20ADV-orange)

```
 PLAY  ▶ 124 BPM  P1.05  ▪▪▪▫▫▫▫▫▫▫▫▫▫▫▫▫
 ┌ LEAD ┐ 03/25                ╭──────────╮
  ACID LEAD                    │ ∿∿∿∿∿∿∿∿ │
  ANALOG  SAW  LPF             ╰──────────╯
  CUTOFF              41%       OCT 4  PENT MIN
  ██████████░░░░░░░░░░░         ARP UP 1/16
  RESO                88%       ▪▪▪▪▪▫▫▫
  ████████████████████░
  ████████████████░░░░░░░░░░░░░░░░░░░░░░░░
 Z..? PLAY  []=CUTOFF  OP=RESO  A=ARP        OCT 4
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

Host unit tests (DSP, sequencer timing, music theory, file format):

```bash
make -C test test
```

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
- `FN` + arrows (`; . , /`) navigate; `FN`+`1`…`8` jumps between modes
- `SHIFT` is the pattern layer: `SHIFT`+`1`…`8` jumps, `SHIFT`+`[`/`]` steps
- `FN`+`Q`/`W`/`E`/`R` generate a beat, bassline, melody or sound

## Modes

| # | Mode | What it is |
| --- | --- | --- |
| 1 | **PLAY** | Live keyboard, scope, cutoff/resonance under your fingers, drum pads on the number row. The "pulled it out of my pocket" screen. |
| 2 | **DRUM** | Nine lanes × up to 64 steps, 16 direct step keys, per-lane tune/decay/tone/level, seven kits. |
| 3 | **SEQ** | Piano-roll step editor for the LEAD and BASS tracks: note, velocity, gate, probability, slide, per-step mute. |
| 4 | **SOUND** | Seven pages of synth parameters for the selected track. |
| 5 | **FX** | Delay, reverb, chorus, drive, master level. |
| 6 | **SONG** | Chain up to 64 pattern slots with repeat counts. |
| 7 | **FILE** | Name, save, load and delete projects on the SD card. |
| 8 | **SYS** | Volume, brightness, swing, scale, root, chord mode, and live CPU / RAM / FPS / battery. |

## The instrument

**Synth** — 8-voice polyphony across two independent tracks (LEAD and BASS),
each with its own patch. Four engines share one parameter set:

- *ANALOG* — two anti-aliased (PolyBLEP) oscillators — saw, square, pulse with
  PWM, triangle, sine, noise — plus a sub oscillator and a noise source.
- *FM* — sine carrier and modulator with a musical ratio and index.
- *WTABLE* — a continuous morph from saw through square and triangle to sine.
- *CHIP* — pulse waves with a 5-bit output crush.

Per voice: an amplitude ADSR, a second ADSR for the filter, a zero-delay
state-variable filter (LP / HP / BP) with resonance, key tracking and bipolar
envelope depth, and an LFO (sine / triangle / square / sample-and-hold / saw)
routable to pitch, filter, amplitude or pulse width. Poly, mono and legato
voice modes with portamento. 25 factory presets across lead, bass, pad, keys,
pluck, chip and experimental groups.

**Drums** — Nine fully synthesised voices (kick, snare, closed and open hat,
clap, tom, rim, crash, perc), each with tune, decay, tone and level, in seven
kits: 808, 909, ELECTRO, MINIMAL, INDUSTRIAL, LOFI, EXPERIMENT. No samples, so
nothing to load off the card.

**Sequencer** — Eight patterns, 1–64 steps each, three tracks per pattern
(LEAD, BASS, and the nine drum lanes). Per-step note, velocity, gate length,
probability, slide and mute. Gates run from a sixteenth of a step up to
seventeen whole steps, so notes can sustain across the bar. Global swing,
BPM 40–300, tap tempo. Patterns
chain into a 64-slot song with repeats.

**Recording** — Arm with `\` and play: notes land on the nearest step of the
running pattern and keep looping. **How long you hold a key is recorded**, so a
sustained note plays back sustained. Hold `BKSP` while the loop passes to rub
notes out again. Switch tracks and keep layering. Nothing is destructive —
every recorded step is editable afterwards on the SEQ page.

**Performance** — Ten scales (chromatic, major, minor, both pentatonics, blues,
dorian, mixolydian, harmonic minor, phrygian) that constrain the keyboard to
the chosen key; chord mode (power / triad / seventh, built from the current
scale); a five-mode arpeggiator with rate, octave range and gate.

**Generators** — Euclidean rhythms, random beats, basslines and melodies that
stay in key, and a patch randomiser that moves only the parameters that keep a
sound musical, within per-parameter ranges.

**Effects** — Stereo-free but dense: a damped delay with feedback, a
four-comb / two-allpass reverb, chorus, drive, and a master peak limiter.
Each synth track and the drum bus has its own delay and reverb send.

## Architecture

```
SynthCard/
  SynthCard.ino          setup()/loop() only
  src/
    app.{h,cpp}          state, key routing, global commands, menu
    audio/
      dsp.h              sample rate, fast sin, soft clip, PolyBLEP, RNG
      patch.{h,cpp}      parameter table, pages, 25 presets, formatting
      voice.{h,cpp}      envelope, SVF filter, LFO, four oscillator engines
      drums.{h,cpp}      nine synthesised drums, seven kits
      effects.{h,cpp}    delay, reverb, chorus, drive, limiter
      engine.{h,cpp}     voice pool, buses, event queue, render task
    sequencer/
      sequencer.{h,cpp}  clock, patterns, song, live recording
    music/
      music.{h,cpp}      scales, arpeggiator, euclid, randomisers
    input/
      keys.{h,cpp}       FIFO draining, key edges, keymap, command layer
    ui/
      ui.{h,cpp}         theme, widgets, chrome, overlays, boot screen
      screens.cpp        the eight screens
    storage/
      storage.{h,cpp}    SD projects, NVS settings
      serialize.cpp      versioned project format
  test/                  host unit tests (no Arduino needed)
  tools/package.sh       build + size check + merged image
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
and the effect settings — ending in an FNV-1a checksum. A project is 9,146
bytes. Loading validates the magic, the version, the checksum, the length and
every field range, so a truncated or corrupt file is reported, not crashed on.

Audio is suspended around every card access: the SPI transfer and FAT
bookkeeping are far too long for the render task.

## Performance notes

Measured with the host benchmark and scaled conservatively (40× for the LX7),
eight voices with all four effects engaged cost roughly **a third of one
240 MHz core**. Static RAM is 100 KB; the 240 × 135 16-bit canvas is another
65 KB from the heap, leaving ~160 KB free. The firmware is ~654 KB, half of a
1.25 MB OTA slot. The UI is capped at ~33 fps so the display push never
competes with audio.

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
- **Not yet run on hardware by the author.** Everything here compiles for the
  ADV, and the DSP, sequencer, generators and file format are covered by 388k
  host assertions, but the audio path itself has not been heard on a device.

## Licence

MIT — see [LICENSE](LICENSE).
