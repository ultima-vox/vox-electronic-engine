# VOX Drums generator: 16 new styles, genre presets and kits — design for approval

Status: **design, documentation only; revision 2 aligned with the owner contract in issue #136**
(2026-11-14) and the hardware UI reference (#135). Where #136 and this document differ, #136
wins. Implementation follows in a separate Draft PR. No code here. Owner request (work session, 2026-11-14):
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
- The grid is straight 16ths in 4/4. Shuffle comes from swing and microtiming.
- **Owner decision (2026-11-14): 3/4, 6/8 and 12/8 are added** (§10). The tables in §3 are the
  4/4 rules; §10.5 gives the rules for the other meters.

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
- UI: in the native hardware-inspired UI (#127, #135). The role controls built earlier in the
  old eight-page editor (branch `drm/pad-roles-ui`, not published) are donors for behaviour
  only, per the #135 contract.

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

- **Nordic Metal** is not one style in common use. **Engineering decision (#136: settle
  details without further questions):**
  - Viking/folk metal reading: a tom-heavy war-drum pulse and mid tempo.
  - Black-metal blast beats are not this style.
  - The listening review can redirect it.
- **Triplet feel:** 6/8 and 12/8 gallops and 3/4 are real meters in the pattern model (§10). They
  are not 16th-grid approximations.

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
- **Acoustic kits (Rock, Metal) need licensed acoustic samples** (#136).
  - The repository has none.
  - Built from the eight synth models, they would not sound like a recorded kit. So they
    **must not ship as finished presets**: they are marked **NOT RELEASE-READY** in the
    manifest until licensed, redistributable samples are added.
  - No sample is downloaded without a verified license and attribution (Freesound included).
- Electronic kits are built from the synth models. Each still passes the listening gate before
  it counts as release-ready.
- **Manifest** per kit and preset: origin, role map, sample licenses, content hashes,
  release-ready yes/no.
- **Patterns and songs per style:**
  - A Song preset per style (5 slots with A/B and the song timeline, 16, 32 or 64 bars).
  - Pattern presets of 1–4 bars (the pattern model's limit).
  - Each is generated from that style's own rules, defaults and a fixed seed, never from
    another style's renamed output, and stored as factory data with committed hashes.
- The existing factory kits and contents keep their hashes. New hashes are added.

## 6. Formats (pre-release policy: one canonical record each)

- **`DGEN` v2:**
  - the style is a stable id 0–22 (0–6 as today, then the 16 new styles in the order of §3).
  - Ids are never reused.
  - The id is a `u8`:
    - 23 used of 256;
    - the encoding stays explicit and bounded;
    - a later style takes the next free id.
  - Decode is strict: an unknown id rejects the restore with a clear error ("unknown generator
    style N"). It never falls back to another style.
  - **No `DGEN` v1 decoder** in the release (#136): a development-build state with v1 is
    rejected like any unsupported version.
- The style registry (id, name, group, tempo range, defaults) is one table in code and is
  tested: exactly 23 unique ids and names, a fixed order, House once.
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

## 9. Decisions

Settled by #136 and the owner session:

- scope (23 styles, three preset kinds);
- `DGEN` v2 without a v1 decoder;
- roles v2;
- acoustic kits only with licensed samples.

Settled here as engineering details:

- Nordic Metal reading;
- style defaults as an undoable button;
- `u8` style id.

Open for review: the per-style rules of §3 (Architecture Guard / tech lead) and the listening
gate per style (owner).

## 10. Meters from the DAW, and triplets together with straight notes (owner decisions 2026-11-14)

Owner decisions:

1. **4/4 is the native meter of every style.** 3/4, 6/8 and 12/8 are additional meters.
2. The plugin **follows the DAW's time signature**.
3. **Triplets must work together with straight notes** in the same pattern (e.g. a straight kick
   with a hi-hat triplet roll).

This is an architecture change. Pattern format, scheduler, song, MIDI export, UI grid and
generator are all affected. It needs Architecture Guard review before code.

### 10.1 Today (`c7c29bb`)

- A stored pattern is 16, 32 or 64 steps per bar, 1–4 bars. Every bar is 4 quarter notes.
- The engine already has a 1/8-triplet step length (`kit::Pattern::timing` = 3), but stored
  patterns never use it.
- The sequencer counts bars of 4 quarters from the host's PPQ. It does not read the host's time
  signature.
- The song is a list of 4/4 bars. MIDI export writes 4/4.
- Ratchet (1–8 hits evenly inside one step) is the only way to play an odd subdivision today.

### 10.2 The meter follows the DAW

- **Source:** the host's time signature and bar position in every block: JUCE
  `PositionInfo::getTimeSignature()` and `getPpqPositionOfLastBarStart()`, from VST3
  `ProcessContext` (`kTimeSigValid`, `kBarPositionValid`). Reading them in `processBlock`
  allocates nothing and does not block.
- **Unverified:** whether Cubase and REAPER fill these fields, also during meter changes, loops
  and offline render. This is the first thing to test.
- **Without host data** (the field is not valid, offline export, tests): the pattern's stored
  meter; 4/4 for a new pattern.
- **Bar lines come from the host.** Each host bar starts the pattern (or song bar) from its
  beginning:
  - A pattern written for a **longer** bar than the host's is cut at the host bar line.
  - A **shorter** one is followed by silence until the next bar line.
  - Nothing is stretched or time-scaled.
  - The UI shows "Host is 7/8, pattern is 4/4: cut at the bar line".
- **Meter changes in the DAW timeline** work bar by bar: every host bar uses its own meter.
- **Song mode:** song bar n plays at host bar n (counting from song position 0). The song has
  no meter of its own; it takes each host bar as it comes.
- **New pattern and Generate** use the host's meter at that moment (the meter selector shows
  "Host (6/8)"). The user can also fix a meter for a pattern (e.g. write 6/8 material while the
  host is 4/4).
- **Supported pattern meters:**
  - numerator 1–16 with denominator 4 or 8: a bar holds at most 16 beats.
  - The generator has rules for 4/4 (every style) plus 3/4, 6/8 and 12/8 (§10.5). In any other
    meter (e.g. 7/8) generation is refused with a message, and manual editing works.
- **MIDI export** writes each pattern's meter as a time-signature event and the bar lengths in
  that meter. A song export writes a time-signature event wherever consecutive bars differ.
- The plugin **never changes the DAW's meter or tempo**.

### 10.3 Triplets together with straight notes: per-beat subdivision

- Triplets are switched **for a piece of the pattern only**, never for the whole pattern or
  song (owner, 2026-11-14). Each **lane** has, for each **beat** of each bar, a subdivision:
  **straight** or **triplet**.
- The other lanes and beats keep theirs. So within one bar:
  - the kick can stay on straight 16ths;
  - the hats play 16th triplets on beat 4 only;
  - the snare plays an 8th-triplet fill over beats 3–4.

| Meter | Beat | Straight cells per beat (resolution 16 / 32 / 64 per 4/4 bar) | Alternative |
|---|---|---|---|
| x/4 (2/4, 3/4, 4/4, …) | quarter | 4 / 8 / 16 | **triplet:** 3 / 6 / 12 (8th, 16th, 32nd triplets) |
| x/8 compound (6/8, 9/8, 12/8) | dotted quarter (3 eighths) | 6 / 12 / 24 … within the cell limit | **duplet:** 4 / 8 / 16 (two or four even notes across the dotted beat) |

- **Ratchet** still divides one cell. A straight 16th cell with ratchet 3 is 32nd-note triplets
  on one note.
- **Microtiming** stays relative to the cell length.
- **Timing is exact:** cell start = beat start + index × beat length / cells.
  - Hits are computed in PPQ as double and sample positions as today.
  - MIDI export at 960 PPQ is exact, because 960 is divisible by 3, 6, 12 and 24.
- **Storage:**
  - per lane and bar, a 16-bit mask of triplet (or duplet) beats;
  - a triplet beat uses fewer cells than a straight one, so the cell array keeps its maximum
    size;
  - for a full pattern (16 lanes × 4 bars) the masks add 128 bytes.
- **Editing:**
  - switching a beat to triplet maps its notes to the nearest triplet cells (and back), as one
    undoable edit;
  - a note that would collide with another is kept and the merge is reported.

### 10.4 Formats (pre-release policy, no shims)

- **`DSEQ` version bump:**
  - per pattern: the meter (numerator, denominator) and whether it is "host" or fixed;
  - per lane and bar: the tuplet beat mask.
  - Strict decode: a cell count that does not fit the meter, an unknown meter, or a mask bit
    beyond the bar's beats rejects the restore.
- **`DSNG` version bump:** no song meter (it follows the host). Bars keep their pattern
  references.
- Development-build `DSEQ`/`DSNG` data is not decoded (as `DGEN`, #136).
- **Budget:** a few hundred bytes more per state. Checked by the boundary fixture (#130 T21).

### 10.5 Generator: 4/4 native, extra meters, triplet rules

**All 23 styles: 4/4 native**, with the §3 rules. Additional meters:

| Style | Additional meters | Rules there |
|---|---|---|
| Nordic Metal | 6/8, 12/8 | 6/8:<br>- kick on 1 and 4 (the two dotted beats), tom 3 doubling them (war drum);<br>- snare on 4 (density < 0.4) or 4 + ghost on 6;<br>- gallop kick 1-2-3 by syncopation;<br>- ride on every eighth |
| Symphonic Metal | 12/8, 3/4 | 12/8:<br>- kick on 1, 4, 7 and 10; snare on 4 and 10, or on every dotted beat in the "epic" bar;<br>- eighth-triplet tom rolls.<br>3/4: kick on 1, snare on 2 and 3, crash on 1 |
| Heavy Metal, Hard Rock | 12/8 | shuffle:<br>- Heavy Metal: kick on each eighth or the 1-3 gallop;<br>- Hard Rock: kick on 1 and 7;<br>- snare on 4 and 10 |
| Symphonic Trance, Dream Trance | 3/4 | kick on 1, soft clap on 3, hats on each eighth (intros and breakdowns) |
| Ambient | 3/4, 6/8 | kick on 1 only; the other hits as in 4/4, probability < 100 % |
| all others | — | 4/4 only. In another host meter, generation is refused with a message |

**Triplet rules inside 4/4**, in the styles where they belong:

- **Trap:** closed-hat 16th-triplet rolls on single beats; this replaces part of today's
  ratchet rolls.
- **Heavy, Symphonic and Nordic Metal:** 8th-triplet tom/snare fills, and triplet double-kick
  bursts at high complexity.
- **Uplifting, Anthem and Symphonic Trance:** an 8th-triplet snare roll before the final
  16th/32nd build in long fills.
- **Hard Rock:** triplet tom fills.
- **DnB, Melodic DnB:** occasional 16th-triplet ghost snares.
- **Every other style: no triplets.** This is a "never" rule. Full-on, Goa, Techno, House and
  Tech House stay straight.

Tests:

- the grammar matrix runs every (style, meter) pair and checks every triplet "always/never";
- unsupported meters must be refused;
- scheduler tests with a simulated host: meter changes, bar starts, loops, mismatched patterns
  cut or padded, triplet cells at exact sample positions at 44.1/48/96 kHz and various block
  sizes;
- export tests: time-signature events, bar lengths, triplet ticks; playback = export within
  1 tick, as today.

### 10.6 UI (native hardware UI, #127 / #135)

- Step Sequencer:
  - the grid groups cells by the beats of the meter;
  - a beat switched to triplet shows its 3/6/12 cells evenly across the same width;
  - a per-lane, per-beat "3" toggle; selecting several beats switches them together.
- Meter display and selector: "Host (6/8)" or a fixed meter. The mismatch notice from §10.2.

### 10.7 Decisions for Architecture Guard

1. Bar lines and meter from the host each block; the stored meter only without host data.
2. Mismatch handling: cut or silence at the host bar line, never stretched.
3. Per-lane, per-beat triplet/duplet subdivision as the way to mix triplets with straight notes;
   ratchet unchanged.
4. Song bars follow host bars; no song meter.
5. `DSEQ`/`DSNG` version bump without decoding development-build data.

## 11. Importable styles (owner request 2026-11-14)

The owner asked that additional styles can be imported into both the generator and the presets.
So a style must be **data**, not C++ code. This is an architecture change and needs
Architecture Guard review before code.

### 11.1 Principle: one style engine for factory and user styles

- A style is a **declarative description**: which role plays where, under which settings, with
  what chance, velocity, accent and ratchet, plus the fill, variation and song rules. One
  interpreter in the generator runs every style.
- The **23 factory styles are written in the same format** and built into the binary as
  resources. They use no hidden code path, so an imported style can do everything a factory
  style can.
- **Identity parity:** the 7 existing styles, rewritten as data, must generate byte-identical
  output. This is checked by the per-style generator digest, which already exists in #132.

### 11.2 File format `.voxstyle` (JSON, version 1)

```
{
  "format": "vox-drums-style", "version": 1,
  "id": "<UUID>", "name": "My Style", "author": "…", "group": "Trance",
  "tempo": { "min": 136, "max": 140 },          // hint only; never changes the host
  "meters": ["4/4", "12/8"], "nativeMeter": "4/4",
  "defaults": { "density": 0.6, "groove": 0.5, "syncopation": 0.3, "swing": 0,
                "humanize": 0.1, "complexity": 0.4, "fills": 0.6, "variation": 0.4 },
  "rules": { "4/4": {
    "lanes": {
      "kick":      [ { "at": [1,5,9,13], "velocity": 1.0, "accent": true } ],
      "clap":      [ { "at": [5,13], "when": { "density": [0.15, 1] }, "velocity": 0.9 } ],
      "closedHat": [ { "at": [2,4,6,8,10,12,14,16],
                       "chance": { "base": 0.35, "density": 0.65 }, "velocity": 0.55 } ],
      "perc1":     [ { "at": [4,8,12,16], "chance": { "base": 0.25, "syncopation": 0.6 },
                       "velocity": 0.6, "ghost": true,
                       "ratchet": { "chance": { "complexity": 0.5 }, "values": [2,3,4] } } ]
    },
    "fill":      { "roles": ["snare", "tom1"], "order": "alternate", "thin": ["closedHat","openHat","perc1","perc2"],
                   "ratchet": { "chance": { "complexity": 0.8 }, "values": [2] } },
    "variation": { "keep": ["kick", "snare", "clap"] },
    "song":      { "breakWithoutKick": true },
    "always":    [ { "role": "kick", "at": [1,5,9,13] } ],
    "never":     [ { "role": "kick", "notAt": [1,5,9,13] }, { "role": "closedHat", "at": [3,7,11,15] } ]
  } }
}
```

- **Positions** are 1-based on the meter's 16th grid:
  - 4/4: 1–16; 3/4 and 6/8: 1–12; 12/8: 1–24.
  - On a finer grid (32 or 64 steps) a rule lands on the matching step, as factory styles do
    today.
- **Triplets:** a rule may target triplet cells of given beats, e.g.
  `{ "beats": [4], "triplet": "16th", "at": [1,2,3,4,5,6] }`. The beat is switched to triplet
  subdivision on that lane (§10.3).
- **Chance** is `clamp (base + Σ coefficient × setting, 0, 1)` over density, groove,
  syncopation, complexity, fills and variation. No other expressions and no scripts.
- **`when`** limits a rule to a settings range. **`bars`** limits it to bar numbers
  (e.g. `"odd"`, `[1]`).
- **Roles** are the semantic roles of §2: kick … tom3, perc1–perc8. They go through the pad
  roles (#132), so an imported style plays correctly on any kit with roles.
- **`always` / `never`** are the style's own invariants. They are checked at import (§11.4).
- Randomness is the generator's deterministic hash, keyed by role slot and position. The same
  style, settings and seed always give the same output.

### 11.3 Limits and validation (strict)

- **Size:** a file is at most 64 KiB. At most 64 rules per role and meter, 4 meters, 24
  positions per rule, 8 ratchet values from 1–8.
- **4/4 required:** a style without `"4/4"` in `meters` and full 4/4 rules is rejected.
  Triple meters are optional additions.
- **Values:** numbers are finite. Velocity is 0–1; coefficients are −2…2; tempo is 40–300.
- **Schema:** an unknown key, a wrong type, an unknown role or meter, a position outside the
  meter, or a duplicate id rejects the file. The error names the field: e.g. "rules.4/4.lanes.clap[0].at[2]: 17 is outside 4/4 (1–16)".
- **Parsing** runs on the message thread only, never on the audio thread, and is bounded in
  time and memory. Untrusted files are data only: nothing is executed.

### 11.4 Import and self-test

- **"Import Style…"** is in the Library and in GENERATE.
- After the schema check, the style is **generated over a settings matrix**:
  - 6 seeds × 3 densities × 3 complexities × 2 fills × every meter it declares;
  - Pattern, Variation, Fill and Song.
- Its `always`/`never` invariants are checked on the result. A style that breaks its own
  invariants is **not imported**, and the report lists the first violations.
- A valid style is copied to `…/ULTIMA VOX/VOX Drums/Styles/<name>.voxstyle`. The project and
  presets do not depend on that file (§11.5).
- **"Export Style…"** writes any style, a factory one included, as `.voxstyle`, for editing
  and sharing.
- **Same id, different content:** the import asks "Replace" or "Keep both". Keep both gives the
  copy a new id.

### 11.5 Registry, projects and presets

- **Style reference** in `DGEN` v2: `u8 kind` (0 factory, 1 user) plus the factory id (§6) or
  the 16-byte UUID. Strict: an unknown factory id or a missing embedded user style rejects the
  restore.
- **Projects embed the user style they use** (new section `DSTL`: the canonical JSON of the one
  style `DGEN` references, at most 64 KiB, with its SHA-256).
  - A project opened on another machine, without the style file, still regenerates identically.
  - If the local library holds the same UUID with other content, the project keeps its
    embedded version and the UI shows "This project uses its own version of My Style".
- **Budget:** `DSTL` adds at most 64 KiB + header. The boundary fixture (#130 T21) must
  include it; the reserve is reviewed with the measured number.
- **Presets:**
  - A new preset type **Style** (`.voxstyle`) appears in the Library next to
    Kit/Pad/Pattern/Song, with Factory and User lists.
  - A Song preset made with a user style embeds `DSTL`, like a project.
  - Generator-settings presets (§4) are the style's `defaults`. "Style defaults" applies them as
    one undoable step.
- The factory content manifest lists the 23 factory styles with their hashes. User styles are
  not in the manifest.

### 11.6 UI (native hardware UI, #127 / #135)

- GENERATE style list grouped by `group`, with Factory and User sections and an "imported" mark.
- Import / Export / Delete (user styles only) and the import report with the field errors and
  self-test violations.
- The tempo range is shown as information. It never changes the DAW.

### 11.7 Tests

- **Parser:**
  - a valid round trip;
  - every rejection class (size, unknown key, type, range, position, role, meter, duplicate id)
    with its exact error text;
  - fuzzing with random bytes and random mutations: no crash, bounded time.
- **Identity:** the 7 existing styles as data give the c7c29bb/#129 digests byte for byte.
- **The 16 new factory styles** pass the grammar matrix and distinction tests of §3 and §10.
- **Import:**
  - a valid style is imported and generates;
  - a style breaking its own `never` is refused with a report;
  - replace / keep both.
- **Projects and presets:**
  - a project with a user style restores and regenerates identically on a "machine" without
    the style file (fresh library folder);
  - a Song preset with `DSTL` behaves the same;
  - a missing `DSTL` rejects the restore.
- **Realtime:** no parsing, file access or allocation on the audio thread (allocation probe).

### 11.8 Decisions for Architecture Guard

1. **One declarative style engine** for factory and user styles, with the 7 existing styles
   proven byte-identical.
2. The `.voxstyle` v1 schema and limits; linear chance formulas only, no scripts.
3. Import self-test against the style's own invariants.
4. Projects and Song presets embed the user style (`DSTL`); budget review with a measured
   fixture.
5. New Library preset type **Style**.
