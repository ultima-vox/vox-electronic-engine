# VOX Drums generator: 16 new styles, genre presets and kits — design for approval

Status: **design proposal, documentation only.** No code. Owner request (work session, 2026-11-14):
add Uplifting Trance, Anthem Trance, Dark Psy, Melodic Trance, Hard Rock, Heavy Metal, Symphonic
Metal, Nordic Metal, House, Tech House, Drum and Bass, Melodic Drum and Bass, Melodic Techno,
Progressive Trance, Dream Trance, Hard Trance and Symphonic Trance.

Owner decisions taken in that session:

- every style gets its own characteristic rhythm rules. Dark Psy must not produce ordinary
  Techno, and Heavy Metal must not be House with a different hit count;
- presets: generator settings per style, factory patterns and songs per style, factory kits per
  genre;
- the pad roles gain Ride, Crash, Tom 2 and Tom 3;
- design first, code after approval.

Policy: pre-release, greenfield (#130 comment 6880138881). No shipped projects exist, so formats
change cleanly, with versioning, and no compatibility shims for the development build #125.

Baseline: `drm/production-candidate` @ `c7c29bb`, plus Draft PRs #129 (Ambient fix, style audit
tests), #132 (roles, engine) and #133 UI branch. House already exists, so this adds **16 styles**,
for 23 in total.

What this document can and cannot settle:

- It fixes **testable rhythm rules** per style. The implementation will prove them on a settings
  matrix, as #128 did.
- Whether a style *sounds* right needs listening (§8). Genre conventions below are written from
  common practice. Rows marked **(?)** are where I am not certain and need the owner's call.

## 1. Notation

- One bar of 4/4 = 16 positions (16ths), numbered 1–16. Beats are 1, 5, 9 and 13. The off-beat
  8ths are 3, 7, 11 and 15.
- "Always" means in every bar, for every setting of the matrix, outside a fill window. "Never"
  means not in any bar, fill windows included, unless the row says otherwise.
- Ratchet = repeats inside one 16th. The engine already supports 1–8.
- The grid is straight 16ths. Shuffle comes from swing and microtiming.
- Triplet grids (12/8, 6/8) are **not** in the pattern model. Styles that use them (some metal)
  get a 16th-grid approximation (§3, Nordic Metal).

## 2. Roles: Ride, Crash, Tom 2, Tom 3

The current roles (#132) are: none, kick, snare, clap, closed hat, open hat, tom, rim, cymbal,
percussion. Proposed set:

| Value | Role | Change |
|---|---|---|
| 0–7 | none, kick, snare, clap, closed hat, open hat, tom, rim | `tom` is shown as **Tom 1 (high)** |
| 8 | crash | the former `cymbal`, renamed |
| 9 | percussion | unchanged |
| 10 | **ride** | new |
| 11 | **tom 2 (mid)** | new |
| 12 | **tom 3 (floor)** | new |

- `DROL` stays one section, version 2: the values 0–12 are valid and decode stays strict. It is
  unreleased, so there is no migration.
- Generator slots:
  - 0 kick, 1 snare, 2 clap, 3 closed hat, 4 open hat, 5 tom 1, 6 rim, 7 crash, 8 ride, 9 tom 2,
    10 tom 3;
  - then percussion #1–#8 in slots 11–18.
  - The first pad with a role takes its slot; a missing role is not written and is noted (as
    #132).
- The **default layout** of 16 pads stays as today: 1–8 kick … crash, 9–16 percussion. It has no
  ride or toms 2–3.
  - A style that uses them warns "No ride pad: ride parts are not generated".
  - Each new genre kit (§5) carries its own roles, with ride and toms where the genre needs them.
- "Set roles from pad models":
  - **toms** by GM note: 48/50 → Tom 1, 45/47 → Tom 2, 41/43 → Tom 3; otherwise by tune, highest
    to lowest;
  - **cymbals**: 49/52/55/57 → Crash, 51/53/59 → Ride; otherwise a "?" row.
- UI (#133 branch): the role lists get the three new entries. Nothing else changes.

## 3. Rhythm rules of the 16 new styles

Columns:

- **BPM** is a tempo hint for presets and listening files. The plugin follows the host tempo.
- **Kick**, **Back-beat**, **Hats / ride**, **Ornaments**, **Fill**.
- **Never** lists the rules that separate the style from its neighbours.

### 3.1 Trance family (four-on-the-floor kick on 1, 5, 9, 13 always, nowhere else outside fills)

| Style | BPM | Back-beat | Hats / ride | Ornaments | Fill | Never |
|---|---|---|---|---|---|---|
| **Uplifting Trance** | 136–140 | clap always on 5, 13 | open hat always on 3, 7, 11, 15; closed hat on 2, 4 of each beat by density | crash on bar 1 of the pattern; light percussion on 8, 16 | **snare roll**: 8ths → 16ths → ratchet 2–4 rising in velocity over the fill window (build-up) | kick off the beat; closed hat on the off-beat 8ths |
| **Anthem Trance** | 138–140 | clap **and** snare always on 5, 13 (layered) | open hat always on off-beats; ride on 8ths when density > 0.6 | crash on bar 1; tom 1–3 descending on the last 4 positions of a fill | snare roll + toms | — (as Uplifting) |
| **Melodic Trance** | 132–138 | clap on 5, 13 when density > 0.1 | open hat always on off-beats; closed hat 16ths soft (velocity ≤ 0.55) | shaker (percussion) 16ths, soft | short snare fill (≤ 4 positions), velocity ≤ 0.8 | velocity of non-kick hits > 0.9 |
| **Progressive Trance** | 128–134 | clap on 5, 13 | closed hat always on off-beats; open hat only on 15 (by density) | **rolling percussion**: perc 1 on 4, 7, 10, 12, 15; perc 2 on 16ths by syncopation; rim on 6, 14 | percussion thinning, short (≤ 4) | open hat on more than one off-beat per bar |
| **Dream Trance** | 136–140 | clap on 5, 13, soft (≤ 0.75) | open hat always on off-beats; **ride 8ths** always; closed hat sparse | crash on bar 1 and on fills | soft tom fill (tom 1→3) | snare roll ratchets above 2 |
| **Hard Trance** | 145–155 | clap + snare always on 5, 13, accented | open hat always on off-beats; closed hat **all 16ths** when density > 0.4 | ride on the beats when complexity > 0.6 | **kick roll**: kick on every 16th of the fill window (the only place the kick leaves the beat) + snare ratchets | kick off the beat outside the fill window |
| **Symphonic Trance** | 136–140 | clap on 5, 13 | open hat always on off-beats | **orchestral toms**: tom 3 on 1 of odd bars, tom 1–3 phrases on 14–16; crash on bar 1 | timpani-like tom roll (tom 3 ratchets) + snare roll | — |

Distinctions that tests check:

- **Uplifting vs Anthem:** the snare lane on 5/13 is always on in Anthem and never on in
  Uplifting outside fills.
- **Melodic vs Uplifting:** the velocity ceiling.
- **Progressive:** rolling percussion; open hat only on 15.
- **Dream:** a ride on every 8th.
- **Hard Trance:** 16th closed hats and kick rolls only in fills.
- **Symphonic Trance:** the three toms.

### 3.2 Psy

| Style | BPM | Back-beat | Hats | Ornaments | Fill | Never |
|---|---|---|---|---|---|---|
| **Dark Psy** | 145–150 | **no clap and no snare on 5/13** except one by chance ≤ 25 % per bar | closed hat on 2, 4 of each beat (16th off-beats) always; open hat on 3, 7, 11, 15 by density > 0.3 | **glitch percussion**: perc 1 and rim on 16ths with ratchet 2–4 (complexity), low velocity; tom 1 rolling on 16ths off the beat | perc / rim ratchet bursts | **closed hat on the beats** (that is Techno's every-8th hat); clap on both 5 and 13 in the same bar; swing > 0 |

- Kick: 1, 5, 9, 13 always, nowhere else.
- Distinctions measured against:
  - **Full-on:** clap on 5/13 in ≥ 90 % of bars there, ≤ 25 % here;
  - **Techno:** Techno has a closed hat on all four beats; Dark Psy never does;
  - Dark Psy has ratcheted percussion in ≥ 30 % of bars at complexity ≥ 0.5.

### 3.3 House and techno family

| Style | BPM | Kick | Back-beat | Hats | Ornaments | Never |
|---|---|---|---|---|---|---|
| **House** (exists) | 120–126 | 1, 5, 9, 13 always; pickup on 16 | clap always on 5, 13 | open hat always on off-beats | rim 4, 7, 12; perc 1 8, 11, 14 | (unchanged) |
| **Tech House** | 124–128 | 1, 5, 9, 13 always, **never elsewhere** | clap or snare on 5, 13 always | closed hat all 16ths with **swing ≥ 0.15** in the style preset; open hat on off-beats | **percussion groove**: perc 1 (bongo) on 4, 7, 10, 14, 16; rim on 11; shaker 16ths | kick pickup on 16; ride |
| **Melodic Techno** | 120–126 | 1, 5, 9, 13 always, never elsewhere | clap/snare on 13; on 5 only when density > 0.6 | closed hat on 16th off-beats (2, 4 of each beat); open hat always on off-beats | **arpeggiated percussion**: perc 1 and tom 1–2 on a 3-step cycle (every 3rd 16th); crash only on bar 1 | closed hat on the beats (Techno's grid) |

- **Techno vs Melodic Techno:** a closed hat on the beats in Techno; never in Melodic Techno.
- **House vs Tech House:**
  - the kick pickup exists only in House;
  - Tech House percussion is on ≥ 4 positions per bar at density ≥ 0.5.

### 3.4 Drum and bass

| Style | BPM | Kick | Snare | Hats / ride | Ornaments | Never |
|---|---|---|---|---|---|---|
| **Drum and Bass** | 170–176 | **1 and 11 always**; 8 by syncopation | 5 and 13 always; **ghost snares** on 4, 8, 10, 15, 16 (complexity) | closed hat 16ths by density, ratchet 2 on 16 (complexity); open hat on 7 | rim on 12 | kick on 5 or 13; kick on 4, 7 or 14 (Breaks' ghost kicks) |
| **Melodic Drum and Bass** | 170–176 | 1 and 11 always; 8 rarely (≤ 0.3) | 5 and 13 always; at most one ghost per bar | **ride 8ths** always, shaker 16ths soft | crash on bar 1 | kick on 5 or 13; more than one snare ghost per bar; velocity > 0.85 outside 5/13 |

Distinctions:

- **Breaks vs DnB:** Breaks has kick ghosts on 4, 7 and 14; DnB never.
- **DnB vs Melodic DnB:** the number of snare ghosts and the ride.
- **Trap vs DnB:** Trap has its snare on 9; DnB on 5 and 13.

### 3.5 Rock and metal (kit roles: kick, snare, closed/open hat, tom 1–3, crash, ride)

| Style | BPM | Kick | Snare | Hats / ride | Crash and toms | Never |
|---|---|---|---|---|---|---|
| **Hard Rock** | 110–140 | 1 and 9 always; 11 or 8 by syncopation; at most 4 per bar outside fills | **5 and 13 always**, accented | closed hat **8ths** (odd positions) always in verse bars; **ride 8ths** instead of hats when density > 0.7 (chorus feel) | crash on 1 of bar 1 and after a fill; **tom fill** tom 1 → tom 2 → tom 3 with snare in the fill window | kick on all four beats in the same bar (the House grid); clap |
| **Heavy Metal** | 120–180 | **double bass**: kick on every 8th (density 0.4–0.75) or every 16th (> 0.75); **gallop** (positions 1, 3, 4 of each beat) when syncopation > 0.6 | 5 and 13 always | ride or crash **on the beats** (density > 0.5); hats 8ths otherwise | crash on 1; tom fills 1→3, kick under the fill | fewer than 8 kick hits per bar at density ≥ 0.5; clap; open hat on the off-beats |
| **Symphonic Metal** | 100–180 | double bass 16ths in odd bars; **half-time** bars (kick 1 and 11) in even bars | 5 and 13 in double-bass bars; **9 only** in half-time bars | ride on 8ths; crash on every beat in the last bar of a phrase ("epic" bar) | tom 3 on the beats under orchestral hits; tom rolls 1→3 in fills | clap; a bar that is neither double-bass nor half-time |
| **Nordic Metal (?)** | 90–140 | **war-drum pulse**: kick + tom 3 on 1, 5, 9, 13; kick gallop (1, 3, 4) by syncopation | 5 and 13 (or 13 only, half-time, when density < 0.4) | ride 8ths; crash on 1 | tom 2 / tom 3 ostinato on 16ths of beat 4 | clap; 16th double bass (that is Heavy Metal) |

- **(?) Nordic Metal** is not one style in common use. The rules above are a Viking/folk
  metal reading: tom-heavy war-drum pulse and mid tempo. Black-metal blast beats (alternating
  kick/snare 16ths, 180+ BPM) would be a different rule set. **Owner: which one is meant?**
- **(?) Triplet feel:** folk and power metal often use 6/8 or 12/8 gallops. The model has
  only straight 16ths, so the gallop above is the 16th version. A triplet grid would be a
  pattern-model change and is not proposed here.

Distinctions that tests check:

- **Heavy Metal vs House:**
  - House: exactly 4 kicks per bar on the beats (plus the pickup) and a clap on 5/13;
  - Heavy Metal: ≥ 8 kicks per bar at density ≥ 0.5, a snare on 5/13 and never a clap.
- **Hard Rock vs Heavy Metal:** ≤ 4 kicks per bar outside fills in Hard Rock.
- **Symphonic Metal:** alternating double-bass and half-time bars.
- **Nordic Metal:** tom 3 on the beats.

### 3.6 Existing styles

Full-on/Psy, Goa, House, Techno, Trap, Breaks and Ambient keep their rules (#128 §1, with the
#129 Ambient fix). Their output does not change.

## 4. Generator settings per style ("style defaults")

Each style gets a default `GeneratorSettings` and a tempo hint:

| Group | Density | Groove | Syncopation | Swing | Humanize | Complexity | Fills | Variation |
|---|---|---|---|---|---|---|---|---|
| Trance (Uplifting, Anthem, Melodic, Dream, Symphonic) | 0.6 | 0.5 | 0.3 | 0 | 0.1 | 0.4 | 0.6 | 0.4 |
| Progressive Trance | 0.55 | 0.5 | 0.55 | 0.05 | 0.15 | 0.5 | 0.4 | 0.3 |
| Hard Trance | 0.75 | 0.6 | 0.3 | 0 | 0.05 | 0.6 | 0.7 | 0.4 |
| Dark Psy | 0.6 | 0.4 | 0.6 | 0 | 0.05 | 0.7 | 0.5 | 0.5 |
| Tech House | 0.6 | 0.6 | 0.6 | 0.2 | 0.2 | 0.5 | 0.4 | 0.4 |
| Melodic Techno | 0.5 | 0.5 | 0.4 | 0 | 0.1 | 0.5 | 0.4 | 0.4 |
| DnB / Melodic DnB | 0.6 / 0.45 | 0.6 | 0.5 / 0.3 | 0 | 0.2 | 0.6 / 0.3 | 0.5 | 0.4 |
| Hard Rock | 0.5 | 0.5 | 0.4 | 0 | 0.3 | 0.4 | 0.6 | 0.4 |
| Heavy / Symphonic / Nordic Metal | 0.8 / 0.7 / 0.5 | 0.6 | 0.4 / 0.3 / 0.6 | 0 | 0.15 | 0.6 | 0.6 | 0.5 |

- The values are starting points for listening (§8), not proven.
- **UI:**
  - choosing a style in GENERATE changes only the style, as today;
  - a **"Style defaults"** button applies the row as one undoable step.
  - Settings undo does not exist today. It is added to the generator history with the same
    mechanism as role edits (#132): a history entry holding the settings before and after.

## 5. Factory content

**Kits per genre** (synth models; no samples):

| Kit | Pads (role) | Used by |
|---|---|---|
| Trance | kick, snare, clap, CH, OH, tom 1–3, crash, ride, 6 perc (shaker, conga, …) | Uplifting, Anthem, Melodic, Progressive, Dream, Symphonic Trance |
| Hard Trance | hard kick, snare, clap, CH, OH, ride, crash, tom 1–3, perc | Hard Trance |
| Dark Psy | short kick, CH, OH, rim, tom 1, crash, 10 glitch perc | Dark Psy |
| Tech House | kick, clap, snare, CH, OH, rim, bongo/conga/shaker perc | Tech House (House keeps "House Techno") |
| Melodic Techno | kick, clap, CH, OH, tom 1–2, crash, tonal perc | Melodic Techno |
| DnB | punchy kick, snare, ghost snare (perc), CH, OH, ride, crash, rim | DnB, Melodic DnB |
| Rock | kick, snare, rim, CH, OH, tom 1–3, crash, ride, + 2 crashes/china as perc | Hard Rock |
| Metal | tight kick, snare, CH, OH, tom 1–3, crash ×2, ride, china (perc) | Heavy, Symphonic, Nordic Metal |

- Each kit stores its roles (`DROL`), so the generator finds ride and toms.
- **Risk:** rock and metal kits are built from the eight synth models (kick, snare, clap, hat, tom,
  rim, cymbal, perc). They will not sound like a recorded acoustic kit. Acoustic sample kits
  need recorded samples, which this project does not have.
- **Patterns and songs per style:** a Song preset per new style (5 slots with A/B and the song
  timeline) is generated with the style defaults and a fixed seed, then stored as factory data
  with committed hashes, like today's factory content.
- The existing factory kits and contents keep their hashes. New hashes are added.

## 6. Formats (pre-release policy: one canonical record each)

- **`DGEN` v2:**
  - the style is a stable id 0–22 (0–6 as today, then the 16 new styles in the order of §3);
  - decode stays strict: an unknown id rejects the restore.
  - `DGEN` v1 (development build) is read once and saved as v2. No shim is kept after the first
    public release.
- **`DROL` v2:** role values 0–12 (§2).
- **Presets:** kit presets carry `DROL`. Song presets carry `DGEN` v2. The preset types are
  unchanged.
- `productStateReserve()` is unchanged: `DGEN` and `DROL` stay the same size.

## 7. Implementation order (after approval)

1. An integration branch with #129, #132 and the #133 UI. The style audit test (#129) is needed
   as the base of the style tests.
2. Roles v2 (§2) in the engine, presets, proposal and UI.
3. `Style` ids 7–22 and their rules (§3); `DGEN` v2.
4. Style defaults with an undoable Apply (§4).
5. Factory kits and the per-style songs (§5); hashes.
6. Tests:
   - the grammar matrix of `suite_drums_generator_style_tests` extended to 23 styles, with every
     "always/never" of §3;
   - the distinction tests of §3.1–3.5, each with its measured statistic in the report;
   - identity parity for the 7 existing styles;
   - roles v2 codec and mapping, UI;
   - MIDI listening files for every style.
7. A Draft PR with Windows CI. No merge without authorization.

## 8. Not proven by any of this

- **Genre authenticity and sound quality.** Rules and tests prove only that each style keeps
  its written grammar and differs measurably from its neighbours. Whether Dark Psy *sounds*
  dark, or whether the metal kit sounds like metal, needs listening. For each style, 3 MIDI
  files at the style's tempo hint will be provided for the owner's review: yes / no / why.
- DAW behaviour is not covered.

## 9. Decisions requested

1. **Approve** the style rules (§3), especially the **Never** column, which is what separates
   styles.
2. **Nordic Metal:** Viking/folk war-drum style as written, or black-metal blast beats?
3. **Roles v2** (§2): rename Cymbal → Crash; add Ride, Tom 2, Tom 3; GM-note proposals.
4. **Style defaults** as a button with undo (§4), not applied automatically on style change.
5. **Factory kits** (§5) from synth models, with the stated limitation for rock and metal.
6. **`DGEN` v2 / `DROL` v2** under the pre-release policy (§6).
