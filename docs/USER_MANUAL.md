# OMNIS — User manual

*Abrasion Labs · VST3 mixing and mastering plug-in · version 0.1*

OMNIS masters your track. It listens to it, understands what you type in English or French,
sets its chain of 8 modules and brings the level where you want it. Everything stays visible
and can be adjusted by hand.

Type **/en** in the AI bar to switch the interface to English (**/fr** for French).

---

## Contents

1. [In 2 minutes](#1-in-2-minutes)
2. [Installation](#2-installation)
3. [First launch](#3-first-launch)
4. [Where to put it](#4-where-to-put-it)
5. [The console](#5-the-console)
6. [AUTO](#6-auto)
7. [The AI bar](#7-the-ai-bar)
8. [References](#8-references)
9. [The 8 modules](#9-the-8-modules)
10. [Honest listening](#10-honest-listening)
11. [Meters](#11-meters)
12. [Latency](#12-latency)
13. [Before you export](#13-before-you-export)
14. [What about tracks?](#14-what-about-tracks)
15. [Mix diagnosis](#15-mix-diagnosis)
16. [Recipes](#16-recipes)
17. [FAQ](#17-faq)
18. [Your data](#18-your-data)
19. [Specifications](#19-specifications)

---

## 1. In 2 minutes

1. Put **OMNIS on the master** (last master slot in FL Studio).
2. Click **AUTO** and play **the whole track**. At the end, stop playback: OMNIS sets the chain,
   tries several loudness settings and keeps the cleanest.
3. Fine-tune with words in the **AI bar**: "more punch", "keep my 808", "a bit quieter",
   "like new wave".
4. Before exporting, type **"export"** in the AI bar: OMNIS tells you if everything is ready.

---

## 2. Installation

**Windows 10 or 11 (64-bit)**: run the installer. OMNIS goes into the common VST3 folder
(`C:\Program Files\Common Files\VST3\OMNIS.vst3`).

**macOS 10.15 or later** (Intel and Apple Silicon): copy the `OMNIS.vst3` folder to
`/Library/Audio/Plug-Ins/VST3/`.

**In FL Studio**: *Options › Manage plugins › Find more plugins* (or *Start scan*). OMNIS shows up
under Abrasion Labs, category *Mastering*. Other VST3 hosts (Ableton Live, Reaper, Bitwig,
Studio One, Cubase…) find it after a rescan.

**Uninstalling** removes the plug-in and keeps your personal folder (your references, what OMNIS
learned). See [§ 18](#18-your-data) to delete it too.

> Format: VST3 only. No AU (Logic) or AAX (Pro Tools) yet.

---

## 3. First launch

- **The guide** opens by itself the first time: 12 steps, about 2 minutes. **NEXT** to go on,
  **SKIP** to close it. The **?** button at the top opens it again, and so do the words **"tuto"**
  or **"help"** in the AI bar. After an update, you are only shown the new steps.
- **Language**: **/en** or **/fr** in the AI bar. Your choice is kept.

---

## 4. Where to put it

OMNIS is a **mastering** plug-in: it works on the whole track. Put it **last on the master**,
after your other effects. Leave some headroom on FL's master (peaks around −6 dBFS, no limiter or
clipper before OMNIS): it needs that room to work well.

---

## 5. The console

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│ abrasion LABS OMNIS   MASTER/COMPOSITION · A B A>B · BEFORE/AFTER · READY TO EXPORT · ? │
├───────────────────────────────────────────────────────────────┬──────────────────┤
│ ▫ 01 / STYLE  RAP FR · RAP US · TRAP · RAGE… · ALL GENRES · REF     [■ AUTO] │ ▫ LEVEL           │
│ AI  your request……………………………………………………………… [APPLY]                │ Streaming −14 …   │
│ OMNIS  its answer                                                │ bars L · R · GR   │
│ 02 / THE CHAIN                                   LATENCY (click) │ LUFS, true peak,  │
│  01 · OBJ · SUB   02 · OBJ · EQ   03 · OBJ · COMP …  (scrolls)   │ range, PLR…       │
│  + SUB · + EQ · + COMP · + SATURATION … (library)               │ ▫ LISTEN: stereo  │
│ 03 / SETTINGS   COMP · VCA  "the clean bus glue"  ON            │ MID SIDE · MATCH  │
│  every setting of the module          │  its screen             │ DELTA · simulated │
│                                                                  │ ▫ GAIN · export   │
└───────────────────────────────────────────────────────────────┴──────────────────┘
```

The interface follows the "OMNIS Rack" mock-up: Space Mono and Space Grotesk fonts (shipped with the
plug-in), black, amber, corner brackets around what is selected.

- **01 / STYLE**: measured styles in one click (AUTO aims at them next); **ALL GENRES…** or **REF**
  opens the window with every genre; **AUTO** on the right.
- **02 / THE CHAIN**: one card per module in the chain: its big knob (main control), its character
  (VCA, Tube, Soft Knee…) and what it is doing right now ("GR −2.1 dB"…).
  - **Click** a card to open all its settings below.
  - **Drag a card** to reorder the chain; **×** removes it.
  - The **library** (under the cards): "+ IMAGER", "+ COMP 4 characters"… puts a module back.
- **03 / SETTINGS**: on the left, every setting of the open module; on the right, its screen
  (curve, gain reduction, nodes to drag).
- **BEFORE / AFTER**: the original sound (OMNIS bypassed) or OMNIS's. **MASTER / COMPOSITION**: the
  console or your buses page.
- **LEVEL** (right): the target loudness in one click, then the meters.
- **Knobs**: drag up or down. **Shift**: fine adjustment. **Double-click**: default value.
- **A / B**: two complete settings of the whole console. **A>B** (or **B>A**) copies the one you
  are hearing into the other.

---

## 6. AUTO

1. Click **AUTO**, then start playback **from the beginning of the track**.
2. OMNIS listens to **the whole track** (the AI bar shows the time heard). At the end, **stop
   playback**: listening ends. You can also click AUTO again to finish early (at least 4 seconds
   heard). Clicking AUTO before any sound arrives cancels it.
3. It sets the whole chain from the full track:
   - towards the **reference** chosen in the REF menu;
   - otherwise towards the **style** you typed ("like rage");
   - otherwise towards the average of all references.
4. **The trials**: on the loudest part it heard (15 seconds, kept in memory during the trials and
   then forgotten), it tries three loudness splits — all limiter, clip 40 / limiter 60,
   clip 70 / limiter 30 —, drives each to the chosen **LEVEL** (right column), measures distortion
   and tonal balance, and keeps **the cleanest** ("AUTO refined: … kept "clip 70 / 30""). A few
   seconds, in the background: the sound keeps playing normally.
5. Finally it fine-tunes the level **by measuring** the output.

| LEVEL | Target (loud part) | For |
|---|---|---|
| Streaming | −14 LUFS | Spotify, YouTube, Deezer: nothing gets turned down by the platform |
| **Balanced** (default) | −11 LUFS | loud but alive: verse / chorus contrast stays |
| Loud | −9 LUFS | a dense beat, already compressed in the project |
| Very loud | −7 LUFS | rage, hard clipping: clipping on purpose |
| Style | the style's or the chosen reference's | following a reference |

**Your track's style.** If you named no style and chose no reference, AUTO **recognises the style
by listening** (trap, drill, rage, plugg / new wave, boom bap, melodic / pop, electro, rock,
hyperpop, soundtrack) and says so: "I hear: trap (64 %)", or, when unsure, its three candidates. It
aims at the first. Type the right style ("it's drill"): **it remembers** and recognises your next
beats better (memory on your computer, measurements only). Ask "what style is it?" any time.

**Your habits.** If, after AUTO, you often tweak the same settings (sub, tilt, Inflator, level,
width, Comp threshold), OMNIS **learns it**: a small neural network, on your computer, links your
tweaks to the beat (its spectrum, width, tempo, style). On the next AUTO runs it applies your
correction itself and says so ("your habit: sub +0.8 dB (learned over 6 sessions)"). It starts
gently (its share grows with the number of sessions) and stays within tight bounds (sub and tilt
±2 dB, Inflator ±15 %, level ±2 dB, width ±20 %, Comp ±6 dB). An "undo" right after AUTO counts as
"wrong choice". Type **"forget my habits"** to erase everything, **"export my learning"** to write
an anonymous file (numbers only) you can share if you want: nothing is sent.

**What you do most, and whether it helps.** Learning only runs after **AUTO** or a **preset** (a
reference or style picked in the REF menu). Each time, OMNIS compares what it proposed with what
you kept, and judges each move with its mastering rules before learning it:
- **inaudible** (too small to hear: less than 0.5 dB of sub…): ignored;
- **hesitation** (moved a lot to end up near the start): ignored;
- **risky** (sub beyond +6 dB, crushed level, widened low end, limiter off…): never learned, and
  OMNIS tells you;
- **artistic choice (art direction)**: against the general rule, but wanted by the style you aim
  for (crushing the level in rage or hard clipping, a dark sound in lo-fi or boom bap, a very wide
  space in ambient…), or kept until export a second time in the same context: that is your art
  direction. Learned for that style only. Turning the limiter off never is one (peaks go over the
  ceiling);
- **useful**: learned; kept until export, it counts double.

A move becomes a **habit** when it comes back (3 times at least) and is useful most of the time.
What happened only once is forgotten over time. Type **"my habits"**: "Over 12 sessions, what you
do most: more sub (7 times), Comp on (4 times). I don't learn: louder (3): inaudible, hesitant or
against mastering rules." Your artistic choices are listed too: "Your artistic choices (art
direction), learned for their style: louder (4)." While you have few habits, AUTO also leans on
the average of OMNIS users for your style ("what others do in trap (12 people): sub +0.4 dB");
its share shrinks as yours grow. That average comes from anonymous contributions (at least 5
people per move) and arrives with updates. The level OMNIS drives itself after AUTO does not count as a tweak.

**The safety net**: OMNIS never compresses more than the level allows. If your beat is very
dynamic, it stops before crushing it and tells you ("Level stopped at …"). To go louder, make it
denser in your project (808 and kick compressed or saturated), then run AUTO again. Changing the
LEVEL during playback brings the level there.

Everything can be adjusted afterwards. AUTO is deliberately gentle: it respects a sound that is
already worked on.

---

## 7. The AI bar

Type what you want, the way you would say it to an engineer. Typos, abbreviations and slang are
understood. Press **Enter** or **APPLY**. OMNIS answers on the right: what it understood and what
it did.

If it has not heard the track yet, it asks you to start playback. It keeps your request and
applies it as soon as it has listened for a few seconds.

### Examples

| You type | OMNIS does |
|---|---|
| "more punch" | more attack (limiter Punch), a little multiband |
| "louder", "-8 lufs", "spotify level" | a loudness target; the limiter is led there step by step |
| "keep my 808", "don't touch the sub" | the low end is protected (the Match EQ barely touches it) |
| "less muddy" | less low-mid |
| "brighter", "more air", "crispy" | presence and highs |
| "warmer", "fatter" | low-mid, a little soft saturation |
| "wider in the highs" | the Imager opens the top (the low end stays mono) |
| "it's sibilant", "harsh s", "de-esser" | a **dynamic EQ** band at 7 kHz that only holds back sibilance |
| "harsh highs", "it hurts my ears" | a dynamic band at 3.5 kHz, plus a small fixed correction |
| "dirtier", "cleaner" | more or less clipper and Inflator |
| "like rage", "like drill" | the chain towards that style's profile |
| "like GRY", "like 2hollis" | the chain towards the artist's style (if known) |

Two requests add up: "more punch" twice gives even more punch. Nuances count: "a touch louder",
"much wider".

### Commands

| You type | Effect |
|---|---|
| **undo** | undoes OMNIS's last action (20 steps) |
| **reset** | every setting goes back to default |
| **export** | says whether the export is ready, or what to change |
| **diagnosis**, "what's wrong" | the mix diagnosis ([§ 15](#15-mix-diagnosis)) |
| **tuto**, **help** | opens the guide again |
| **/en**, **/fr** | interface language |

---

## 8. References

### The REF menu

Next to the AI bar. It lets you aim at a **style**, an **artist** or a **track**:

- **Styles**: the genre profiles that ship with OMNIS (for example *Rap Français & New Wave ›
  plugg fr*). Each profile is the average of at least 3 measured tracks. No audio is shipped, only
  measurements.
- **Your tracks**: the ones you added (see below).
- A click on REF opens **every genre**: families as tabs, then one tile per sub-genre, each with its
  artwork (drawn by OMNIS from the genre's character and its profile curve; no image is shipped).
  "profile · 26 tracks": a measured profile; "guide: the family": no profile yet, OMNIS aims at
  the family. Click a tile for its sheet (artists, your references), then **USE THIS GENRE**.
- **"None"** hands control back to your words.

Choosing a reference sets the chain right away: Match EQ curve, punch, mono low end, verse /
chorus contrast, clipping, level. AUTO then aims at it. The choice is saved with the project.

### Adding a reference file

1. **REF › "Add a reference file…"**, then pick a WAV, MP3 or FLAC.
2. **Name the file "Artist - Title"** (or "Title - Artist"). OMNIS first reads the file's tags
   (ID3, FLAC, WAV), then its name.
3. It measures it in the background. The audio is **never kept**: only measurements are added to
   your personal library. The track then appears in the REF menu.

**MusicBrainz**: to confirm the artist and title, OMNIS can query MusicBrainz, an open music
database. This is **OMNIS's only Internet connection**: the artist and title are sent, nothing
else. You can turn it off in the REF menu ("MusicBrainz lookup: off"). Small artists may be
missing from MusicBrainz: the file is still added under the name you gave it.

> Use files you are allowed to use: a track you bought, your own production.

### Letting OMNIS hear a reference played in the DAW (advanced)

1. Send the reference track to OMNIS's **auxiliary (sidechain) input**, without routing it to the
   master.
2. Turn on the **Listen Reference** parameter (in your DAW's parameter list for the plug-in) and
   play at least **60 seconds**, then turn it off.
3. The reference is measured and added to your personal library.

---

## 9. The 8 modules

Default order: **Sub → Match EQ → Comp → Inflator → Imager → Multiband → Clipper → Limiter**. Each
module has its **ON** switch; when it is off, it does not touch the sound. Comp arrives **off**:
turn it on when you want glue.

### 1. Sub — infrasonics, mono low end, sub

| Setting | Range | Default | Role |
|---|---|---|---|
| Low Cut | 5–60 Hz | 16 Hz | removes useless infrasonics |
| Mono Below | 20–300 Hz | 120 Hz | below this, the low end is mono (club, phone) |
| Mono Amount | 0–100 % | 100 % | how much mono |
| Mono Slope | 24 / 48 dB/oct | 24 | steepness of the split |
| Sub Gain / Sub Freq | −12…+6 dB / 30–150 Hz | 0 dB / 70 Hz | low shelf |
| Sub Synth | 0–100 % | 0 % | an octave below, for a thin 808 |
| Sub Phase | Linear / Analog | Linear | Linear keeps the 808's waveform (stable peaks) |

### 2. Match EQ — tonal balance, fixed and dynamic

The **EQ | DYN** switch at the top of the page chooses the view.

**EQ view**: a linear-phase curve, separately in Mid and Side (21 nodes per channel, half an
octave each, ±12 dB). The AI bar and references write it; you touch it up by dragging the nodes on
the screen.
- **Amount** (0–100 %) scales the whole curve; **Tilt** (±6 dB) tilts it.
- **Character**: **Clean** (linear phase, precise: the default) or **Analog** (wider, softer bells,
  minimum phase like an analog EQ: no pre-echo in front of the kick and the 808 when you boost the
  low end). Same latency. AI bar: "analog EQ", "linear phase".
- **MID / SIDE**: the channel you are editing (the other one is drawn in blue).
- **FLAT**: resets the edited channel to 0 dB.
- The **TARGET REFERENCE** card reminds you what you are aiming at.

**DYN view: the dynamic EQ.** 4 bands that **only act when a sound sticks out**: sibilance, an
808 note that swells, a piercing hi-hat. The rest of the time they do nothing.
- Each band compares its level to **its usual balance in the mix**. It does not depend on how
  loud your track is: no dBFS threshold to set.

| Setting | Range | Default | Role |
|---|---|---|---|
| ACTIVE | on / off | off | turns the band on |
| Channel | Stereo / Mid / Side | per band | what the band listens to and corrects |
| Freq | 20 Hz – 20 kHz | 70 · 300 · 3500 · 7000 Hz | band centre |
| Q | 0.3–8 | 1.4 · 1 · 1.2 · 2 | width (higher = narrower) |
| Range | −18…+12 dB | −4 dB | how far it goes: **negative = holds back**, **positive = lifts** |
| Threshold | 0–18 dB | 4 dB | how far above its usual level the band must rise before acting |
| Attack / Release | 0.5–50 ms / 20–1000 ms | 5 / 150 ms | speed, shared by the 4 bands |

- **On the screen**: drag a node left or right for the frequency, up or down for the range.
  **Double-click** a node to turn its band on or off. The pale curve shows the range, the filled
  curve what the band is doing right now.
- **RIGHT NOW** shows the gain being applied live. In the rack, the Match EQ card shows
  "DYN −x dB".
- **No added latency.** With a band off, the sound is untouched.

### 3. Inflator — density and warmth

Makes the sound denser and fuller **without raising the peaks**. The 808 (below Split) and the
rest of the mix are set separately.

| Setting | Range | Default | Role |
|---|---|---|---|
| Inflate 808 / Inflate Mix | 0–100 % | 0 % | amount below / above Split |
| Split | 60–300 Hz | 120 Hz | 808 / mix split |
| Drive | ±12 dB | 0 dB | push into the curve |
| Curve | −50…+50 | 0 | −50: very dense and soft; +50: nearly linear, then hard |
| Warmth | 0–100 % | 0 % | even harmonics (the "warm") |
| Output | −12…+6 dB | 0 dB | output level |
| Oversampling | 1x–16x | 8x | quality against aliasing |
| Character | Inflate · Tape · Tube · Transistor | Inflate | the colour of the saturation (below) |

**The characters** (switching during playback does not click):
- **Inflate**: density, the peak stays where it is. The "loudest" to the ear.
- **Tape**: gently squeezes the peaks, discreet odd harmonics. Glues, softens.
- **Tube**: even harmonics, rounder and warmer. Good on an 808 or cold melodies.
- **Transistor**: clean until pushed, then biting (high harmonics). For grit.

In the AI bar: "tube saturation", "tape saturation", "transistor".

### 4. Imager — stereo width per band

Side width in 4 bands (splits at 250 Hz, 2 kHz and 8 kHz by default). **0 % = mono,
100 % = unchanged, 200 % = very wide.** The centre (808, kick, lead) is never touched.

### 5. Multiband — holding the 808 over time

A 4-band compressor (sub, low, mid, high; splits at 120 Hz, 800 Hz, 5 kHz, draggable on the
screen). Per band: **Threshold** (−60–0 dBFS), **Ratio** (1–20:1), **Attack** (1–300 ms),
**Release** (10–3000 ms), **Makeup** (±12 dB). Neutral by default (1:1 ratio). The big knob in
the rack (**GLUE**) sets all four bands together.

### 6. Clipper — clipping peaks before the limiter

| Setting | Range | Default | Role |
|---|---|---|---|
| Mode | Hard / Soft Knee / Diode | Soft Knee | clipping shape (Diode: warmer) |
| Drive | 0–24 dB | 0 dB | push into the clipper |
| Ceiling | −12–0 dBFS | 0 dBFS | clipping level |
| Knee | 0–100 % | 50 % | softness of the knee |
| Oversampling | 1x–16x | 8x | quality against aliasing |
| Split | 60–300 Hz | 120 Hz | the 808 is clipped on its own, without dirtying the melodies |
| Sub Drive | ±12 dB | 0 dB | more (or less) clipping on the 808 only |
| Sub Make-Room | 0–100 % | 0 % | the 808 gives way a little when everything hits together |

### 7. Limiter — the final ceiling

| Setting | Range | Default | Role |
|---|---|---|---|
| Gain | 0–24 dB | 0 dB | push into the limiter (the level) |
| Ceiling | −6–0 dBTP | −1 dBTP | **true peak** ceiling, always held |
| Release | 1–1000 ms | 60 ms | base release |
| Auto Release | 0–100 % | 100 % | adaptive release (slower when limiting for long) |
| Punch | 0–100 % | 0 % | emphasises attacks before limiting: more impact |
| Upward | 0–12 dB | 0 dB | raises the quiet passages |
| Unlink Transient / Sustain | 0–100 % | 0 % | left and right limited separately (attacks / sustain) |

### Changing the order

Drag a module by its name in the rack. The sound briefly switches to the original signal while
the chain is reordered, with no click. Example: the Clipper after the Limiter for a more
aggressive sound. In that case the ceiling is held to the sample, but true peak is no longer
guaranteed.

---

### 8. Comp — bus compressor (the glue)

A compressor for the whole mix, with no latency and the same gain on left and right. Its detector
listens to the track **without the low end** (SC High-Pass): the 808 does not make it pump.

| Setting | Range | Default | Role |
|---|---|---|---|
| Threshold | −40…0 dB | −18 dB | above it, it compresses (read as peak) |
| Ratio | 1–10 :1 | 2 :1 | strength of the compression |
| Attack / Release | 0.1–100 ms / 20–2000 ms | 10 / 200 ms | how fast it acts / recovers |
| Makeup | 0–12 dB | 0 dB | gives back the lost level |
| Mix | 0–100 % | 100 % | < 100 %: parallel compression (keeps the attacks) |
| SC High-Pass | 20–300 Hz | 90 Hz | the low end stays out of the detector |
| Character | Opto · FET · VCA · Vari-mu | VCA | how it compresses (below) |

- **Opto**: gentle; recovers fast after a short hit, slowly when it has been compressing for long.
- **FET**: fast and biting (peak detection); to make drums snap.
- **VCA**: the clean bus glue, the most neutral.
- **Vari-mu**: very round, the compression builds up gradually; to smooth a track.

Big **SQUEEZE** knob: from nothing to strong (threshold 0 → −36 dB), part of the level given back.

## 10. Honest listening

| Button | Use |
|---|---|
| **MATCH** | listen at the **same level** as the original: judge the sound, not the volume. It blinks: remember to turn it off |
| **DELTA** | hear **only what the open module changes** |
| **STEREO / MID / SIDE** | listen to the centre or the sides alone |
| **BYPASS** | all of OMNIS bypassed, with no click and no offset (latency stays the same) |
| **A / B** | compare two complete settings |
| **LISTEN** (right column) | hear your master as on another system |

**Simulated listening**: AirPods (2/3), AirPods Pro 2, AirPods Max, Beats Studio Pro,
Sony WH-1000XM5, Galaxy Buds2 Pro, JBL Tune 510BT, Sennheiser HD 600, Audio-Technica M50x,
phone speaker, TikTok on a phone, car, club PA, spatial (binaural), and the loudness
normalisation of Spotify (−14 LUFS), Deezer (−15), YouTube (−14) and Apple Music (−16).
- The menu turns **red** while one is active. It is **never exported** and never saved with the
  project.
- These are approximations based on published characteristics, not measurements of the devices.
  Brand names are only used to identify the models.

---

## 11. Meters

MASTER column, each meter with a verdict in words (GOOD, LOUD, DENSE, PUNCHY…).

| Meter | What it tells you |
|---|---|
| **LUFS-S / LUFS-I** | loudness over 3 s / over everything played (BS.1770) |
| **TRUE PEAK** | the real peak after conversion; keep it **below −1 dBTP** for streaming |
| **MAX PEAK** | the highest sample peak |
| **PLR** | peak-to-loudness ratio: the bigger, the more it breathes |
| **LRA** | loudness range between quiet and loud parts |
| **CORR** | correlation: near 1 = mono, below 0 = phase problem |
| **GR** | limiter gain reduction |

---

## 12. Latency

- **Max quality** (default): about **710 ms** at 48 and 96 kHz (767 ms at 44.1 kHz). That is the
  price of very precise linear phase in the low end. The DAW compensates it automatically: the
  sound stays in time.
- **WORK mode**: click **LAT** at the top. Latency drops to about **4 ms** (3.4 ms at 96 kHz): you
  can play and record live with OMNIS on the master. Quality is reduced (shorter minimum-phase
  filters, harder limiter and multiband on transients). The box **blinks red** while it is on.
  **Switch back to max quality before exporting.**

In practice: write and record without OMNIS or in Work mode, master in Max quality.

---

## 13. Before you export

Type **"export"** in the AI bar, or look at the **READY TO EXPORT** banner above the analyser.
OMNIS checks:

- simulated listening on **Off**;
- **MATCH** and **DELTA** off, **STEREO** monitoring;
- **WORK** mode off;
- **BYPASS** off;
- true peak below 0 dBTP (ideally below −1 dBTP);
- reasonable loudness (above −6 LUFS, it warns you).

> **16-bit export** (CD, some platforms): OMNIS does not add dither yet. Turn on your DAW's
> dither when exporting. At 24-bit or floating point, it is not needed.

---

## 14. What about tracks?

OMNIS masters on the master. It can also **listen to your buses**, without touching their sound,
and tell you what is in each one (**COMPOSITION** page, button at the bottom right). **Nothing to
put on the tracks**: your mix is not touched.

1. On each **bus** (DRUMS, 808, MELODY, EFX…), put OMNIS and choose **ROLE: Bus** (COMPOSITION
   page, or type "role bus" in the AI bar).
2. Play the song **from start to end**. Each bus listens to the whole song, each second only once:
   replaying a loop does not count its hits twice, and the page says what has been heard ("song
   heard: 2:27"). Every OMNIS (the master and the buses) shows each bus, with its name in FL, its
   group and what OMNIS hears in it:
   - **DRUMS**: kick, snare / clap, hi-hat, open hat (with the number of hits);
   - **808 / BASS**: the 808 (how long it is held);
   - **MELODY**: lead / synth (attacked notes), pad (a held sound);
   - **EFX**: reverse cymbal (rises then stops dead), riser (rises for a long time), impact (big,
     deep, wide hit that fades slowly).

In **Bus**, the sound passes **untouched**, with no latency. FL does not tell plug-ins which tracks
feed a bus: OMNIS therefore does not know your track names, it **hears** what the bus contains. A
bus's group comes from its name ("DRUMS", "MELO", "EFX", "808"…), otherwise from what it contains.
Type "composition" in the AI bar for the full list. Changed your mix? **RELISTEN** (COMPOSITION
page, or "relisten" in the AI bar): every bus starts over.

What is better fixed track by track (an 808 that is too loud, low-mid build-up), the **mix
diagnosis** tells you, so you can fix it in your project.

---

## 15. Mix diagnosis

**MIX DIAGNOSIS** button (bottom right), or "diagnosis" in the AI bar. OMNIS compares your track
(what it heard) to the chosen reference, or to the average of the references. It tells you what
**the master cannot fix**, to correct in your project:
- **Important**: mix already hitting 0 dBFS, mix already crushed, low end not mono;
- **Advice**: mono loss, 808 too loud or missing, low-mid build-up, mids, presence, low-end
  attack;
- **Info**: air, the note held by the 808.

It never fixes anything on its own: some "flaws" are intentional, and you decide.

---

## 16. Recipes

**A quick first master.** REF › your style. AUTO, then play the whole track. Then "a bit quieter" or
"louder" to taste. "export" before rendering.

**A hi-hat or lead that hisses.** "it's sibilant" in the AI bar. Not enough? Say it again: the band digs 2 dB
deeper each time (down to −12 dB). By hand: Match EQ › DYN, band 4, Mid channel, 6–8 kHz,
Range −6 dB.

**An 808 that swells on some notes.** Match EQ › DYN, band 1, Stereo, Freq on the note that
sticks out (50–80 Hz), Q 2, Range −3 dB, Threshold 3 dB.

**Make the snare crack.** A dynamic band at 200 Hz (body) or 2–4 kHz (crack), **positive Range**
(+3 dB), Attack 1 ms, Release 80 ms: it only lifts on the hits.

**Louder without crushing.** Type "louder" rather than pushing the limiter all the way. OMNIS
splits the work between the Clipper (peaks) and the Limiter (the rest), watching the PLR. For a
very loud, dirty sound: "dirtier", Clipper in Hard, a little Sub Drive.

**Check in mono and on a phone.** LISTEN › *Phone speaker*, then *TikTok (phone)*. If the 808
disappears: a little **Sub Synth** in the Sub module, or a little **Inflate 808**.

---

## 17. FAQ

**OMNIS says "start playback".** It needs to hear your track for a few seconds before acting.
Your request is kept and applied as soon as it has listened.

**The latency shown is very large.** That is normal in Max quality ([§ 12](#12-latency)). The DAW
compensates it. To play live: WORK mode.

**MusicBrainz does not find my track.** Small artists are often missing. The file is still added
with the name you gave it ("Artist - Title"). With no name and no tags, OMNIS asks you to rename
the file.

**I hear no difference.** Check the modules' **ON** lights, **BYPASS**, and that simulated
listening is **Off**. A neutral module (default settings) lets the sound through unchanged.

**The level changes when I compare.** Clicking **BEFORE** turns **MATCH** on by itself: BEFORE and AFTER at the same level, you judge the sound. Turn MATCH off to hear the real level again.

**I broke everything.** "undo" (step by step), or "reset".

**How much CPU?** On one core at 48 kHz: about 13 % at rest, 16 % with every module active.

---

## 18. Your data

- **No telemetry.** OMNIS sends nothing, except the artist and title of a file you add, to
  MusicBrainz, if that lookup is on.
- **Your personal folder**:
  - Windows: `%APPDATA%\Abrasion Labs\OMNIS`
  - macOS: `~/Library/Application Support/Abrasion Labs/OMNIS`
- It holds the references you added (measurements, never audio), the artwork you supplied, your
  preferences and what OMNIS learned from your feedback. Delete it to start fresh.
- `apprentissage/`: the log of your sessions (`journal.tsv`: beat measurements, proposed settings
  and your tweaks — never audio, never a file name), the learned network (`reseau.txt`) and the
  graph of what you do most (`graphe.tsv`: setting names and numbers; "export my learning" writes
  a copy, `graphe_usages.json`, readable in OMNIS's data page). "forget my habits" deletes them.
- The DAW project keeps your settings and the chosen reference.

---

## 19. Specifications

| | |
|---|---|
| Format | VST3, Windows 64-bit and macOS 10.15+ (Intel, Apple Silicon) |
| Audio | stereo, 32- and 64-bit, all common sample rates (tested from 22.05 to 384 kHz) |
| Latency | Max quality ≈ 710 ms · Work ≈ 4 ms |
| Chain | 8 reorderable modules (including a bus compressor with 4 characters), 4-band dynamic EQ built into the Match EQ |
| Meters | BS.1770 loudness (LUFS M / S / I), LRA, true peak, PLR, correlation |
| Simulated listening | 18, plus Off |
| Languages | English, French |
| Network | MusicBrainz only, when adding a file, can be turned off |
| Publisher | Abrasion Labs · https://onirique.store |

Legal notices and component licences: `LICENSE_NOTICES.md`, shipped with the plug-in.
