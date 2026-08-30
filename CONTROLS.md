# SynthCard — Controls

Everything is one keypress deep. `FN` is the command layer, `SHIFT` is the
pattern layer, and without either the bottom two rows are always a musical
keyboard.

**Press `` ` `` at any time for the manual of the tab you are on.** That page
lists every key for that screen, and `FN`+`,` / `FN`+`/` flips to the global
keys — so nothing here has to be memorised.

## Musical keyboard

Rows 3 and 4 are a piano, laid out the way trackers have done it for decades.
`-` / `=` move the octave.

```
  black:      S  D     G  H  J     L  ;
  white:    Z  X  C  V  B  N  M  ,  .  /
  note:     C  D  E  F  G  A  B  C' D' E'
```

Seventeen semitones, C through E an octave up. Hold `SHIFT` while playing for
an accent (velocity 127), `ALT` for a soft note (62); otherwise 100.

On the **DRUM** page these same keys become nine drum pads
(`Z X C V B N M , .` = kick, snare, closed hat, open hat, clap, tom, rim,
crash, perc).

## Transport and navigation

| Key | Action |
| --- | --- |
| `SPACE` | Play / stop |
| `FN`+`SPACE` | Tap tempo |
| `\` | Record arm (starts the transport if stopped) |
| `FN`+`\` | Song mode on / off |
| `TAB` | Next mode |
| `FN`+`TAB` | Previous mode |
| `FN`+`1`…`8` | Jump to mode 1–8 (PLAY DRUM SEQ SOUND FX SONG FILE SYS) |
| `` ` `` | **Manual for the tab you are on** (press again to close) |
| `FN`+`` ` `` | Menu |
| `FN`+`ENTER` | Menu |
| `FN`+`;` `.` `,` `/` | Up / down / left / right (the arrows printed on the keycaps) |
| `ENTER` | Confirm / toggle |
| `BKSP` | Delete / back |

## Always-live controls

| Key | Action |
| --- | --- |
| `9` / `0` | BPM −1 / +1 (`FN` = ±10) |
| `-` / `=` | Octave down / up |
| `O` / `P` | Select previous / next parameter |
| `[` / `]` | Value − / + (`FN` = coarse) |
| `A` | Arpeggiator on / off |
| `FN`+`A` | Arpeggiator mode (up / down / up-down / random / order) |
| `F` / `K` | Previous / next sound (drum kit on the DRUM page) |
| `'` | Mute the current track / lane |
| `FN`+`BKSP` | Clear the current track / lane |
| `SHIFT`+`1`…`8` | Go to pattern 1–8 (queued until the bar ends while playing) |
| `SHIFT`+`[` / `SHIFT`+`]` | Previous / next pattern |
| Hold `BKSP` while playing | Erase whatever is under the playhead |

## Recording

1. `\` arms the recorder and starts the transport. The top bar turns red.
2. Play. Notes land on the nearest step of the pattern that is running and
   loop straight back at you.
3. **Held length is recorded.** Hold a key for two steps and it plays back two
   steps long, not as a stub.
4. Wrong note? **Hold `BKSP`** while it loops past and it is rubbed out.
5. `\` again disarms. Everything you recorded is normal pattern data — edit
   any of it on the SEQ page.

`FN`+`BKSP` clears the whole track. In chord mode only the root is recorded,
because a sequencer track plays one note at a time.

## Step keys

`1`–`8` are steps 1–8, `Q`–`I` are steps 9–16. On the DRUM page they toggle
the step under the current lane; on the SEQ page they move the step cursor.
On the PLAY page they fire the first eight drum lanes as finger pads.

Patterns longer than 16 steps page with `FN`+`,` / `FN`+`/`.

## Generators (FN + top letter row)

| Key | Action |
| --- | --- |
| `FN`+`Q` | Random drum beat |
| `FN`+`W` | Random bassline (in the current scale) |
| `FN`+`E` | Random lead melody (in the current scale) |
| `FN`+`R` | Randomise the current sound, within musical ranges |
| `FN`+`T` | Euclidean rhythm on the selected drum lane (press to add a hit) |
| `FN`+`Y` / `FN`+`U` | Copy / paste pattern |
| `FN`+`I` | Clear pattern |
| `FN`+`O` / `FN`+`P` | Save / load project |

## Per-page notes

**PLAY** — `[`/`]` is cutoff, `O`/`P` is resonance, `FN`+`;`/`.` switches
between the LEAD and BASS track. The scope shows the master output.

**DRUM** — `FN`+`;`/`.` picks the lane, `O`/`P` picks a lane parameter
(tune / decay / tone / level) and `[`/`]` adjusts it, auditioning as you go.

**SEQ** — `FN`+`;`/`.` picks the track. Playing a note key writes it into the
selected step and advances. `O`/`P` picks the field (note / velocity / gate /
probability), `[`/`]` edits it. `ENTER` mutes a step, `BKSP` clears it.

The pattern bank is drawn across the top of this page: the lit slot is what
you are editing, and `SHIFT`+`1`…`8` or `SHIFT`+`[`/`]` moves between them.
Patterns longer than 16 steps show `PG 1/2`; `FN`+`,`/`/` turns the page.

**Note length** — the `GATE` field runs from `1/16` of a step up to `16/16`,
then keeps going in whole steps: `2STP`, `3STP` … `17STP`. So a pad can hold
across a whole bar. Long notes draw as a bar across the steps they cover.

**SOUND** — Seven pages of synth parameters. `FN`+`,`/`/` turns the page,
`1`–`7` jumps straight to one. `O`/`P` picks a parameter, `[`/`]` edits.

**FX** — Twelve master parameters in two columns; same cursor and value keys.

**SONG** — `FN`+`;`/`.` moves through the arrangement, `O`/`P` picks the
pattern or repeat field, `[`/`]` edits, `ENTER` appends a slot, `BKSP` removes
one. `1`–`8` sets the slot's pattern directly.

**FILE** — `FN`+`,`/`/` picks NAME / SAVE / LOAD / DELETE / NEW. With NAME
selected the whole letter keyboard types (`A`–`Z`, `0`–`9`, `-`, `_`, upper
cased so the field always matches the filename); `BKSP` deletes and `ENTER`
moves on to SAVE. `SPACE`, `TAB` and the `FN` layer stay commands while you
type. `FN`+`;`/`.` scans the card and walks the file list.

**SYS** — Volume, brightness, swing, scale, root, chord mode, plus live CPU,
voice count, free RAM, frame rate and battery.
