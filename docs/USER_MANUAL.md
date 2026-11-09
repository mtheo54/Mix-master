# OMNIS — User manual

*Abrasion Labs · VST3 mixing and mastering plug-in · version 0.1*

OMNIS masters your track. It listens to it, understands what you type in English or French,
sets its chain of 7 modules and brings the level where you want it. Everything stays visible
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
9. [The 7 modules](#9-the-7-modules)
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
┌──────────────────────────────────────────────────────────────┬──────────────┐
│ AUTO · A · B · A>B · BYPASS · MATCH · DELTA · STEREO/MID/SIDE │   MASTER     │
│ ? · LAT                                                       │ input/output │
├──────────────────────────────────────────────────────────────┤ peaks, LUFS  │
│ AI [ your request……………… ] ▼ REF [ reference ] APPLY   answer  │ true peak,   │
├──────────────────────────────────────────────────────────────┤ PLR, LRA…    │
│ RACK: Sub · Match EQ · Inflator · Imager · Multiband ·        │              │
│       Clipper · Limiter                                       │ LISTEN       │
├──────────────────────────────────────────────────────────────┤ DIAGNOSIS    │
│ DRAWER: settings of the open module  │  its screen            │ EXPORT       │
├──────────────────────────────────────────────────────────────┤ MIX DIAGNOSIS│
│ ANALYSER (output spectrum, export banner) · GONIOMETER        │              │
└──────────────────────────────────────────────────────────────┴──────────────┘
```

- **The rack**: each module has a big knob (its main control), an **ON** light and a small screen
  that says what it is doing right now ("GR −2.1 dB", "CLIP −0.4 dB"…).
  - **Click** a module to open all its settings in the drawer.
  - **Drag a module by its name** to change the order of the chain.
- **The drawer**: on the left, every setting of the open module; on the right, its screen (curve,
  gain reduction, nodes to drag).
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
- **The funnel** (left of REF) filters the menu: a family, then a sub-genre.
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

## 9. The 7 modules

Default order: **Sub → Match EQ → Inflator → Imager → Multiband → Clipper → Limiter**. Each module
has its **ON** switch; when it is off, it does not touch the sound.

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
- **WORK mode**: click **LAT** at the top. Latency drops to about **90 ms** (67 ms at 96 kHz) to
  play live, but quality is reduced (less precise low end). The box **blinks red** while it is on.
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

OMNIS only goes on the master: it does not process tracks one by one. What is better fixed track
by track (an 808 that is too loud, low-mid build-up), the **mix diagnosis** tells you, so you can
fix it in your project.

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

**The level changes when I compare.** Turn on **MATCH**: you compare at the same level.

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
- The DAW project keeps your settings and the chosen reference.

---

## 19. Specifications

| | |
|---|---|
| Format | VST3, Windows 64-bit and macOS 10.15+ (Intel, Apple Silicon) |
| Audio | stereo, 32- and 64-bit, all common sample rates (tested from 22.05 to 384 kHz) |
| Latency | Max quality ≈ 710 ms · Work ≈ 90 ms |
| Chain | 7 reorderable modules, 4-band dynamic EQ built into the Match EQ |
| Meters | BS.1770 loudness (LUFS M / S / I), LRA, true peak, PLR, correlation |
| Simulated listening | 18, plus Off |
| Languages | English, French |
| Network | MusicBrainz only, when adding a file, can be turned off |
| Publisher | Abrasion Labs · https://onirique.store |

Legal notices and component licences: `LICENSE_NOTICES.md`, shipped with the plug-in.
