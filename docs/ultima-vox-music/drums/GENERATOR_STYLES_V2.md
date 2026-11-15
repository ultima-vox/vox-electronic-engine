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

## 2. Roles on 16 pads (revision 3: fixes the 19-on-16 P1 of the #137 review)

Review finding (#137 comment 6880526397, #136 comment 6880518298):

- revision 2 listed 11 instrument slots + 8 percussion slots = 19 on a 16-pad instrument;
- the fix needs a hard maximum of 16 physical pads with a deterministic per-kit map;
- no mandatory instrument may be dropped or aliased silently;
- roles must stay separate from MIDI notes.

### 2.1 Three separate things

| Concept | What it is | Count |
|---|---|---|
| **Pad** (lane) | a physical pad of the instrument: sound, MIDI note, output, pattern lane | **exactly 16**, indices 0–15. Nothing is ever generated, stored or exported for an index ≥ 16. |
| **Role** | per-pad metadata saying which instrument part the pad plays | one per pad (`DROL`) |
| **Part** | a line a style writes: kick, snare, …, perc 3 | per style, at most 16 (§2.4) |

- A **MIDI note** is a property of the pad. It is set by the user or the kit, not by the role.
  - Changing a pad's note never changes its role.
  - Changing a role never changes a note.
- The generator writes **parts**. The role map turns each part into **one pad index (0–15) or
  none**.
- Export writes each pad's own note. So a generated or exported note can only target an existing
  pad, and the pad's current note.

### 2.2 Role values (`DROL` v2, strict 0–12)

| Value | Role |
|---|---|
| 0 | none |
| 1–7 | kick, snare, clap, closed hat, open hat, tom 1 (high), rim |
| 8 | crash (was "cymbal") |
| 9 | percussion |
| 10 | ride |
| 11 | tom 2 (mid) |
| 12 | tom 3 (floor) |

- **`DROL` v2 codec:**
  - `u32 version = 2`, `u8 explicit (0/1)`, `u8 role[16]` (0–12): exactly 21 bytes;
  - strict: a wrong size, version, flag or role value rejects the restore;
  - written only when the roles are explicit;
  - no v1 decoder (pre-release policy). It is unreleased, so there is no migration.
- 16 bytes of roles plus the header; the strict decode rejects values > 12.

### 2.3 Concrete 16-pad kit maps

Every factory kit stores its map. Notes are GM where one exists.

| Pad | Default / electronic (today) | Trance | Rock | Metal |
|---|---|---|---|---|
| 1 | kick (36) | kick (36) | kick (36) | kick (36) |
| 2 | snare (38) | snare (38) | snare (38) | snare (38) |
| 3 | clap (39) | clap (39) | rim / cross-stick (37) | rim (37) |
| 4 | closed hat (42) | closed hat (42) | closed hat (42) | closed hat (42) |
| 5 | open hat (46) | open hat (46) | open hat (46) | open hat (46) |
| 6 | tom 1 (45) | tom 1 (50) | tom 1 (48) | tom 1 (48) |
| 7 | rim (37) | rim (37) | tom 2 (45) | tom 2 (45) |
| 8 | crash (49) | crash (49) | tom 3 (43) | tom 3 (41) |
| 9 | perc (50) | ride (51) | crash (49) | crash (49) |
| 10 | perc (51) | tom 2 (47) | ride (51) | ride (51) |
| 11 | perc (52) | tom 3 (43) | perc: crash 2 (57) | perc: crash 2 (57) |
| 12 | perc (53) | perc: shaker (82) | perc: ride bell (53) | perc: china (52) |
| 13 | perc (54) | perc: conga (63) | perc: splash (55) | perc: splash (55) |
| 14 | perc (55) | perc: tambourine (54) | perc: pedal hat (44) | perc: ride bell (53) |
| 15 | perc (56) | perc: reverse crash / FX (60) | perc: cowbell (56) | perc: pedal hat (44) |
| 16 | perc (57) | perc: snare roll layer (40) | perc: tambourine (54) | perc: snare rim-shot layer (40) |
| **Roles present** | 8 instruments + 8 perc | 11 instruments + 5 perc | 10 instruments (no clap) + 6 perc | 10 instruments (no clap) + 6 perc |

- The **Dark Psy, Tech House, Melodic Techno and DnB kits** use the default/electronic column,
  with their own sounds and percussion names.
- **Metal double bass** is the one kick part on pad 1. It is played on 8ths or 16ths with
  alternating velocity (left/right foot). It is not a second kick role.
- Every factory kit map fits in 16 pads, with no duplicate instrument roles.
- A test checks every factory map:
  - roles valid;
  - at most one pad per instrument role;
  - every part each style marks as **required** (§2.4) has a pad in that style's factory kit.

### 2.4 Parts per style: required, optional, never

Each style declares its parts:

| Group | Required parts | Optional parts | Never |
|---|---|---|---|
| Trance family | kick, clap, open hat, closed hat | snare, crash, ride, tom 1–3, rim, perc 1–5 | — |
| Dark Psy | kick, closed hat, perc 1 | open hat, rim, tom 1, crash, perc 2–8 | clap on 5 and 13 in the same bar |
| Tech House, House, Melodic Techno | kick, clap or snare, closed hat, open hat | rim, crash, tom 1–2, perc 1–6 | — |
| DnB, Melodic DnB | kick, snare, closed hat | open hat, ride, crash, rim, perc 1–4 | — |
| Hard Rock | kick, snare, closed hat, crash | open hat, ride, tom 1–3, rim, perc 1–6 | clap |
| Heavy, Symphonic, Nordic Metal | kick, snare, crash, ride, tom 1, tom 3 | closed/open hat, tom 2, rim, perc 1–6 | clap |

- **At most 16 parts** per style, and at most 8 of them percussion. Perc *n* goes to the *n*-th
  percussion pad in pad order.
- **Missing required part** (the kit has no pad with that role):
  - the part is not generated;
  - the Generator panel shows a warning with the role, e.g. "Heavy Metal needs a ride: no pad
    has the Ride role", and a button **Assign…** that opens the role selector;
  - the generator does not substitute another pad.
- **Missing optional part:** not generated. A quiet note lists them ("Not used: tom 2, perc 6").
- **Notices are style-aware:** only parts the selected style uses are reported. A rock kit
  without a clap gets no clap warning, because no rock style uses a clap.

### 2.5 Duplicate roles: deterministic, visible, under user control

- Two or more pads with the same instrument role:
  - the **lowest pad index** plays the part; the others get nothing from the generator, but
    keep their manual notes;
  - a notice reads "2 kick pads: pad 1 plays generated kicks", with one action, **Use pad N**.
    Both pads hold the same role, so a swap would change nothing. **As approved on #143:** the pad
    that plays the part now gets the role **None** and keeps its manual notes, and pad N (now the
    first pad with the role) plays it. One undoable step (undo and redo). Its tooltip says exactly
    this: "Use pad N: pad M gets the role None and keeps its notes; pad N plays the generated …
    hits. One undo step."
- Percussion is not a duplicate: several percussion pads take perc 1, 2, … in pad order.
- **Full generation** (Pattern, Variation, Song) clears unlocked lanes of pads that receive no
  part, as approved in #132 (Q2). Mutate touches only the selected pads.

### 2.6 Role proposal from pad models

"Set roles from pad models" stays explicit, with "?" rows and Apply/Cancel, one undo step (#132).
New hints:

- **toms** by GM note: 48/50 → tom 1, 45/47 → tom 2, 41/43 → tom 3; otherwise by tune, highest
  to lowest. **As approved on #143:** this is a *proposal* only. The default/legacy layout keeps
  pad 6 (note 45) = tom 1; "Set roles from pad models" shows pad 6 tom 1 → tom 2 as a difference
  (`RoleProposal::changedPads`), Cancel changes nothing, Apply is one undoable step;
- **cymbals**: 49/52/55/57 → crash, 51/53/59 → ride; otherwise a "?" row;
- two crash candidates: the first is crash, the second is percussion.

### 2.7 Tests (added to R1–R19 of #132)

- **Bounds:**
  - for every style, kit map, seed and setting in the matrix, every generated hit has a pad
    index 0–15;
  - every exported Note On equals the note of that pad at export time;
  - with notes reassigned after generation, the export follows the new notes and the roles are
    unchanged.
- **Factory maps:** valid; no duplicate instrument role; required parts present for each
  style's kit.
- **Missing and duplicate:**
  - a missing required part: no hits, a warning with Assign;
  - a duplicate: the lowest pad plays it, notice, Use pad N swaps in one undo step;
  - style-aware notices: no clap warning for rock and metal.

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

## 7. Task order (TL decision, #136 comment 6880518298 and #137 comment 6880526397)

**Now (independent foundation, its own Draft code PR):**

1. A declarative style registry, parser and interpreter skeleton, with exactly **23 canonical
   style ids and names** (House once). Ids are stable; style metadata and defaults live in one
   place.
2. The 7 existing styles ported to declarative definitions, with **byte-identical** results for
   Pattern, A/B Variation, Fill, Mutate and Song, locks and seeds included. The old code and its
   outputs are **test references only**, not shipped migration code.
3. Tests:
   - ids and names unique;
   - deterministic hashes;
   - bounded parser errors;
   - no allocation on the audio thread.
4. No on-disk format is frozen in this step (`DGEN` v2 and `DSTL` wait for Architecture Guard).

**In parallel:** one focused Architecture Guard review of this document:

- §2 (16-pad roles);
- §3 (rules);
- §10 (host meter, mixed tuplets, scheduler and export);
- §11 (import, `DGEN` v2 and `DSTL`, portable presets).

The result is one consistent v1.0 schema with a test matrix, approved before the irreversible
parts are written.

**Then, against the reviewed spec:**

- roles v2;
- meters and tuplets;
- import;
- the 16 new styles;
- style defaults;
- presets and factory content;
- MIDI listening files.

**Assets:**

- The developer researches acoustic drum samples with verifiable redistribution and
  commercial rights: CC0 preferred, CC BY with full attribution.
- For each asset, record URL, author, licence version, retrieval date, checksum and NOTICE text.
- Until a licensed, credible kit exists, Rock/Metal kits and their content are
  **NOT RELEASE-READY**. If none can be found, a shortage report follows.
- Grammar and test work does not wait for this.

**UI:** a separate workstream after the owner's final visual sign-off (#127, #135): native
custom JUCE components, never stock JUCE painting or the old eight-page editor.

**Acceptance:** Windows CI on the exact head of each PR; one final integrated Cubase/REAPER
acceptance. No merge without authorization.

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

## 10. Meters, mixed tuplets and timing (revision 7: continuous Swing, sample offset, packed state)

Owner decisions (2026-11-14):

- **4/4 is native for every style**; 3/4, 6/8 and 12/8 are additional meters;
- the plugin **follows the DAW's time signature**;
- **triplets only for a piece of a pattern**, mixed with straight notes.

Not implemented before Architecture Guard PASS.

### 10.1 Today (`c7c29bb`)

- A stored pattern has 16, 32 or 64 cells per bar and 1–4 bars, in a dense array of
  16 lanes × 4 bars × 64 cells.
- Each cell holds one event (`PatternStep`: on, accent, velocity, probability, ratchet 1–8,
  micro ±50 % of a cell, locked).
- Every bar is 4 quarter notes. The sequencer does not read the host's time signature.

### 10.2 Storage model: per-beat subdivision within a fixed 64-slot bar

**Meter and beats.** A pattern has a meter n/d:

- d = 4: n beats of a quarter;
- d = 8 with n ∈ {6, 9, 12}: compound, n/3 beats of a dotted quarter;
- d = 8 otherwise (e.g. 7/8): n beats of an eighth.
- At most 16 beats.

**Resolution.** Per pattern: 8th, 16th, 32nd or 64th. Each beat of each lane has one of two
subdivisions:

| Beat kind | Straight cells per beat (8th / 16th / 32nd / 64th) | Alternative cells per beat |
|---|---|---|
| quarter (x/4) | 2 / 4 / 8 / 16 | **triplet** 3 / 6 / 12 / 24 |
| dotted quarter (6/8, 9/8, 12/8) | 3 / 6 / 12 / 24 | **duplet** 2 / 4 / 8 / 16 |
| eighth (5/8, 7/8, …) | 1 / 2 / 4 / 8 | triplet — / 3 / 6 / 12 |

- **Slots per beat** = max(straight, alternative) at that resolution. The beat's events use the
  first *k* slots of its subdivision. Bar capacity = beats × slots per beat.
- That must be **≤ 64**, the existing per-bar storage. Nothing is truncated: a combination over
  64 is **not offered**.

**Capacity per meter** (slots per bar; **✗** = not offered, with the reason shown in the
resolution menu):

| Meter | 8th | 16th | 32nd | 64th |
|---|---|---|---|---|
| 4/4 | 12 | 24 | 48 | ✗ with tuplets (96); **64, straight only** (tuplet switch disabled: "64th notes: triplets unavailable") |
| 3/4 | 9 | 18 | 36 | ✗ with tuplets (72); **48, straight only** |
| 6/8 | 6 | 12 | 24 | 48 |
| 12/8 | 12 | 24 | 48 | ✗ (96): "12/8 goes down to 32nd notes" |
| 7/8 | 7 (straight only) | 21 | 42 | ✗ with triplets (84); **56, straight only** |

- **Events per cell:** one, as today: velocity, accent, probability, ratchet, micro, lock.
- **Timing of a cell (new release):** `eventTick = barStartTick + beatStartTick + cellIndex * cellTicks + microTicks` on the canonical **960-PPQ** grid. Cell start and offset are integer ticks; the shared `BarTiming` module (§10.5a) supplies event ticks for playback and MIDI export. Do not retain uniform-step or percentage-micro timing in the new v1 state.

**MIDI and exactness.**

- Beat lengths at 960 PPQ: 960 for a quarter, 1440 for a dotted quarter, 480 for an eighth.
- Every cell count above divides them exactly (960 / 24 = 40, 1440 / 24 = 60, 480 / 12 = 40), so
  NoteOn ticks are integers.
- NoteOff is sent at min(cell length, the next hit on the same lane) — no overlap, as today.

### 10.3 Changing a beat's subdivision never loses or merges events (revision 5)

Correction after AG review 5906187802:

- Today `PatternStep::micro` is an **integer percent** of a cell (signed byte, ±50, edited in
  whole percent). It cannot hold offsets such as −1/3 of a cell, so revision 4's "exact via
  micro" was not true.
- In the first-release `DSEQ`, an event's offset is **`microTicks`**:
  - a signed 16-bit offset in ticks of the canonical 960-PPQ timeline, relative to its cell's
    start;
  - bounded to ±½ of that cell's length.
  - The largest cell is 480 ticks (an 8th), so the bound is at most ±240, well inside 16 bits.
- The UI may show percent. A user edit is quantised **once** to whole ticks, and every later
  operation keeps those ticks.

**Switching a beat (or a selection of beats) between straight and tuplet:**

1. Every event's **absolute tick** = cell start tick + microTicks; it is kept.
2. Each event goes to the cell whose start is nearest (ties to the earlier cell), with
   microTicks = absolute tick − new cell start.
3. The whole switch is **rejected atomically**, with nothing changed and the steps named, when
   any of these holds:
   - two events would share a cell;
   - a |microTicks| would exceed ½ of the new cell;
   - a locked event would not stay one locked event;
   - the result would not fit the capacity of §10.2.
4. Otherwise the switch is one undo step. **Undo/Redo**, not a second independent subdivision conversion, restores the saved event, lock, mask and tick bytes **bit-identically**.

All ordinary cell starts at these resolutions are integer ticks; user micro edits are quantised once. A reverse subdivision switch is **not guaranteed to restore the original cell index**: in 4/4, a straight cell starting at tick 480 with `microTicks = -120` sounds at tick 360. Switching to 16th-triplets can represent it from tick 320 with `microTicks = +40`. Switching back, tick 360 is exactly between straight starts 240 and 480; the chosen earlier-cell tie rule selects 240, not the original 480. Both conversions preserve the **absolute note time**, but the cell index changes. **Undo/Redo must restore bit-identical state; two independent opposite edits need only preserve all absolute event ticks and other event attributes**, and must reject collisions. Include half-cell ties in the test matrix. Ratchet rounding is separately specified in §10.5a.

### 10.4 Meter from the host, and what playback may never do

- Each block reads the host's time signature and bar position (JUCE
  `getTimeSignature()`, `getPpqPositionOfLastBarStart()`, `getBarCount()`). This is read-only:
  no allocation, no lock.
- **Missing data:**
  - no time signature → the pattern's own meter;
  - no bar position → bars counted from song position 0 in the pattern meter;
  - the UI shows "Host gives no bar positions; bars counted from the start".
- **Mismatch:** a pattern whose meter differs from the host bar plays from each host bar line.
  - A longer pattern bar is cut at the next host bar line; a shorter one rests until it.
  - Nothing is stretched.
  - This is a **playback rule only**: it never edits the pattern, the song or the export.
- **Loop or jump** (PPQ moves backwards, or forwards by more than one block):
  - the scheduler drops its queued events for the old position and starts from the new host
    bar;
  - drum voices are one-shots and keep decaying, so there are no hanging notes. The plugin has
    no MIDI output, so there are no queued NoteOffs to send.
- **Meter change in the host:** the song bar index follows the host bar count
  (`getBarCount()`), or the counted bars when it is missing. A meter change between two
  bars only changes how the next bar is cut or padded.
- **Export is independent of the host:** a pattern exports in its own meter; a song exports bar
  by bar in each bar's pattern meter, with a time-signature event wherever it changes.
- **Tests:**
  - a simulated host with meter changes, loops, jumps and missing fields;
  - NoteOn sample positions at 44.1/48/96 kHz with blocks of 64/333/512;
  - export ticks;
  - export unchanged by any playback mismatch.
  - Cubase/REAPER behaviour: in the final integrated DAW acceptance (not per PR). Whether they
    report bar positions during loops and meter changes is **unverified**.

### 10.5 Formats

- **`DSEQ`, new version:**
  - per pattern: meter (n, d), resolution, and whether the meter follows the host;
  - per lane and bar: a 16-bit subdivision mask (one bit per beat);
  - cells, at most 64 slots per bar. `microTicks` is signed int16 **in memory**; on disk DSEQ/DSNG cells are fixed 4-byte little-endian words containing flags (3 bits), ratchet minus one (3), velocity (7), probability (7), microTicks signed 9-bit two's complement (9), reserved zero (3). The implementation must explicitly document bit positions and golden vectors; do not use implementation-defined bitfields.
  - **reserved but inactive slots** (the slots of a beat beyond its current subdivision's cell
    count) must be empty: any content there rejects the restore.
  - Strict decode rejects unknown meter/resolution, invalid mask or capacity, malformed inactive slots, probability/ratchet/velocity range errors, microTicks beyond ±half actual cell, nonzero packed high bits, truncated/trailing bytes. Rejection is transactional.
- `DSNG`: no song meter (bars follow the host); version bump.
- Development-build versions are not decoded (#136 policy).
- **State budget (binding TL #140):** the uncompressed six-byte cell codec exceeded the 736-KiB product reserve in the measured worst-case fixture; the **four-byte packed codec is approved** for the first release. Reported estimates after packing are DSEQ ~165,218 bytes and DSNG ~264,714 bytes but **final budget PASS is pending**. Keep the reserve unchanged initially. Measure complete DSEQ/DSNG + 64-KiB DSTL + DROL + maximum legal sample references + preserved unknown-section allowance, headers and checksums, with 10 full patterns and 64 full unique Song bars. Include malformed input and exact round-trip tests in #130 T21.

### 10.5a One shared timing engine (revision 5, AG review 5906187802)

- Today `PatternStore` (`validSteps`, `timingFor`, `StoredPattern::view`), the KitEngine
  scheduler (`stepLength`), the exports and the editor each assume 16/32/64 steps of one length.
- Revision 4 capacities (12, 18, 21, 24, 36, 42, 48, 56, …) and per-beat subdivisions break that
  assumption everywhere.
- **One module computes all timing.** Pure, allocation-free, no locks, usable on the audio
  thread:

  ```
  struct BarTiming {        // from (meter n/d, resolution, the lane's subdivision mask for the bar)
      int beats, barTicks;                  // e.g. 12/8 → 4 beats, 5760 ticks
      int slotsPerBeat;                     // max(straight, tuplet) at the resolution
      int cells (int beat) const;           // active cells of that beat
      int cellStartTick (int beat, int cell) const;   // exact integer ticks from the bar start
      int cellTicks (int beat) const;       // beat ticks / cells
      int slot (int beat, int cell) const;  // storage slot 0..63
  };
  ```

- **Consumers:**
  - the scheduler: hit tick = bar start tick + cellStartTick + microTicks, with ratchet subhits calculated from the cell's rational tick duration using the exact same quantisation rule as export;
  - the generator: positions to cells;
  - the editor: geometry and editing;
  - pattern and song MIDI export: unswung events/ratchets share integer ticks with playback; **continuous swung** audio is converted once to the nearest whole 960-PPQ tick (maximum 0.5-tick difference), as explicitly approved in #140;
  - the subdivision switch.
- No subsystem reconstructs timing on its own. The uniform `stepLength` arithmetic is
  removed.
- **Ticks to samples:**
  - Ordinary cell-start + `microTicks` note positions are integral. **Ratchet subhits need not be integral before rounding** (e.g. a 40-tick cell divided by 3). A single allocation-free integer/rational helper must map each subhit to the **nearest whole tick, with exact ties to the earlier tick**, and both scheduler and MIDI export must consume these quantised events, maintaining increasing order and rejecting a combination if two distinct subhits cannot be represented without collision.
  - **Audio sample conversion** retains the verified `ceil((eventPpq - blockStartPpq) * samplesPerQuarter - 1e-6)` rule (first sample at/after the event), not `floor`. Inputs may be exact ticks / 960 or continuous PPQ after Swing; export rounds Swing once but realtime retains it. Verify DRM-00 parity, fractional PPQ, negative/block boundary cases at 44.1/48/96 kHz and blocks 64/333/512.
  - **Swing (TL #140 option A):** shared continuous `swing × cellTicks` delay on alternating **straight** cells, never automatically on tuplet cells. Specify phase per beat/bar; reproduce old 4/4 uniform-grid floating operation order so frozen kit parity stays bit-identical. Export rounds absolute swung time once (ties earlier); Swing = 0 keeps exact tick equality. Test 0, 0.21 and 0.5 in Pattern/Song and mixed subdivisions.
- **Host jumps:**
  - a discontinuity is a PPQ step backwards of more than 1 tick, or forwards of more than one
    block plus 1 tick;
  - smaller differences are floating-point jitter and do not reset the scheduler.

### 10.5b Test matrix for revision 7 (before `DSEQ` is frozen)

| Area | Cases |
|---|---|
| Capacity | All allowed meters/resolutions and 4-byte cell format; measured max DSEQ/DSNG/DSTL/DROL/sample references plus preserved-state allowance ≤736 KiB before PASS |
| Subdivision switches | 4/4 16th ↔ 16th-triplet and 32nd ↔ 32nd-triplet; 3/4 likewise; 6/8 and 12/8 straight ↔ duplet; on-grid and humanised/off-grid notes at ±½ cell. Any accepted conversion preserves absolute ticks and all event attributes; a second inverse edit can choose a different cell on a tie. **Undo/Redo alone** must restore bit-identical state. |
| Rejections | two events into one cell; a \|microTicks\| > ½ cell; a locked step that would split or move; each leaves cells, mask, ticks and history unchanged |
| Undo/redo | every accepted switch; bit-identical restore |
| Reserved slots | inactive slots remain empty after all operations; DSEQ with nonempty inactive slots is rejected. Old `StoredPattern::resize` drops off-grid events and MUST NOT be used for new meter/resolution changes; conversions are lossless-or-atomic-reject. |
| Scheduler | mixed straight/tuplet notes; one ceil(x−1e-6) sample conversion at 44.1/48/96 kHz, blocks 64/333/512; continuous swung audio and DRM-00 parity |
| Export | Same integer tick events when unswung; Swing rounded once to 960-PPQ tick (≤0.5-tick difference); correct NoteOn/Off and time-signature events |
| Host | meter changes, loops, jumps, transport jitter and missing fields; export independent from host; first/last beat micro offsets crossing bar boundaries must be scheduled/exported coherently, or rejected clearly before editing, never silently omitted |

### 10.6 Generator

- All 23 styles: **4/4 native**. Additional meters, and triplet rules inside 4/4, as in
  revision 3:
  - Nordic Metal: 6/8 and 12/8;
  - Symphonic Metal: 12/8 and 3/4;
  - Heavy Metal and Hard Rock: 12/8;
  - Symphonic and Dream Trance: 3/4;
  - Ambient: 3/4 and 6/8;
  - triplet fills and rolls only in Trap, the Metal styles, Hard Rock, Uplifting / Anthem /
    Symphonic Trance and DnB; "never" in the others.
- Generating in a meter the style does not define is refused with a message. A rule never
  writes beyond the capacity of §10.2.
- **Tests:**
  - every (style, meter, resolution) the style supports;
  - the grammar matrix;
  - every generated hit inside the bar's capacity;
  - subdivision switching preserves absolute hit time; **Undo/Redo** restores the exact cell state, not necessarily a separate inverse edit.

### 10.7 UI

- Grid cells grouped by beat; a tuplet beat shows its cells across the same width.
- A per-lane, per-beat "3" (or "2" in compound meters) toggle; several selected beats switch as
  one undo step.
- Refusals are shown at the place of the edit. The resolution menu marks the unavailable
  combinations of §10.2 with their reason.

## 11. Importable styles: one canonical `.voxstyle` schema (revision 4: answers AG-1)

- **One schema, one parser, one AST** for factory and user styles. It **extends the flat-rule
  schema already implemented and parity-tested in #138**. There is no second, incompatible "v1".
- The 7 factory definitions in #138 move to this schema with byte-identical generation. The
  #138 test proves it.

### 11.1 Schema (`"format": "vox-drums-style"`, `"version": 1`, canonical public form)

```
{
  "format": "vox-drums-style", "version": 1,
  "id": "<UUID>",                       // required; factory styles have fixed UUIDs in the registry
  "key": "house", "name": "House",      // key: [a-z0-9-], unique per id; name: shown
  "meta": { "author": "…", "group": "House & Techno", "tempo": { "min": 120, "max": 126 } },
  "meters": ["4/4"],                    // must contain "4/4"; also "3/4", "6/8", "12/8"
  "defaults": { "density": 0.6, …, "variation": 0.4 },
  "parts": {                            // required (§2.4; approved on #143): every part a rule, the fill
    "required": ["kick", ["clap", "snare"], "openHat"],   // or an assertion uses; an inner list = alternatives
    "optional": ["snare", "closedHat", "rim", "perc1"]    // at most 16 parts, 8 percussion; not in contentHash
  },
  "rules": [                            // flat list, as #138
    { "part": "kick", "at": [1, 5, 9, 13], "velocity": 0.95, "accent": true },
    { "part": "closedHat", "at": [2, 4, …], "kind": "ghost", "chance": "0.1 + 0.7 * density", "velocity": 0.4 },
    { "part": "snare", "meter": "12/8", "at": [4, 10], "velocity": 0.9 },
    { "part": "closedHat", "beats": [4], "subdivision": "tuplet", "at": [1, 2, 3, 4, 5, 6], … }
  ],
  "probability": { "value": "90 - 50 * complexity", "min": 30, "max": 100, "except": ["kick"] },
  "fill": { "cycle": ["clap", "snare", "snare"], "ratchet": 2 },
  "variation": { "keep": ["kick", "snare", "clap"] },
  "song": { "breakWithoutKick": true },
  "assertions": [ { "part": "kick", "never": [2, 3, 4, …] } ]   // optional, see 11.3
}
```

- **Rule `meter`** defaults to `"4/4"`.
- **`at` positions:**
  - on a straight beat: 1-based on the meter's 16th grid (4/4 1–16, 3/4 and 6/8 1–12, 12/8
    1–24);
  - with `"beats"` and `"subdivision": "tuplet"`: cell indices of the tuplet at the pattern's
    resolution, counted within each listed beat.
- **Formulas:** exactly #138's bounded arithmetic: numbers, the 8 settings, `+ - *`, parentheses,
  64 tokens, depth 8. Products like `0.15 + 0.55 * syncopation * (0.5 + density)` are allowed,
  so imported styles can do what factory styles do. Revision 3's "linear only" is withdrawn.
- **Strict:**
  - any unknown key at any level rejects the file, with the field path;
  - the limits of #138 apply, plus `meta` text ≤ 64 characters and `assertions` ≤ 64.
- **Canonical serializer:** the AST is written back as JSON with sorted keys, fixed spacing and
  shortest round-trip numbers (`std::to_chars`), so parse → write → parse is exact.
- **Content hash:** SHA-256 of the canonical binary form, as #138 implements. It covers the
  generation-relevant data; `meta` is hashed separately for display.

### 11.2 Identity, collisions and projects

- **Id:**
  - every style has a UUID; factory UUIDs are constants in the registry;
  - the registry's numeric ids 0–22 stay for display order only;
  - **`DGEN` v2 stores the style UUID** (16 bytes). There is no factory/user kind byte, so the
    reference has one form.
- **Library import:**

  | Case | Result |
  |---|---|
  | same UUID, same content hash | "already in the library" (no change) |
  | same UUID, different hash | the user chooses **Replace** (the library entry changes; projects keep their own embedded copy) or **Keep both** (the copy gets a new UUID; its name gets " (2)") |
  | a factory UUID | refused: "factory styles cannot be replaced" |
  | another UUID with the same name | allowed; the list shows the author |

- **Projects and Song presets embed the user style they use** (`DSTL` = canonical JSON + its
  hash, at most 64 KiB, one per state).
  - On restore the embedded copy is used whatever the local library holds.
  - If the library has the same UUID with another hash, the UI says "This project uses its own
    version of My Style".
  - A user style referenced in `DGEN` without `DSTL` rejects the restore. Factory UUIDs need no
    `DSTL`.

### 11.3 What validation proves, and what it does not

- **Schema and security:** strict parsing, bounds, message thread only, no code or script; 4,000+
  random mutations without a crash (#138 already does this for the rule part).
- **Structural self-consistency:** the optional `assertions` (always/never per part) are checked
  on a generated matrix at import. A style that breaks its **own** assertions is refused.
  - This proves only that the file agrees with itself. It is **not** a genre or quality check,
    and the UI must not call it one.
- **Genre and sound:** only the owner's listening review, for factory styles; nothing for user
  styles.

### 11.4 Required tests (in the implementation PR)

1. **Factory round trip:** each factory style exported → parsed → identical AST hash →
   byte-identical generation over the #138 parity matrix.
2. **User style through a project:**
   - library import → project `DSTL` → save;
   - restore into a fresh instance with an **empty library folder** → identical generation and
     identical `DSTL` bytes.
3. **Collisions:** every row of the table in 11.2. **Missing `DSTL`:** the restore is rejected
   and the live state is unchanged.
4. Every rejection class with its field path, the limits, and fuzzing of whole files including
   `meta`, `meters`, tuplet rules and assertions.
5. A user style is not offered in a meter it does not define, and a tuplet rule never writes
   outside §10.2 capacity.

### 11.5 Decisions for Architecture Guard (revision 4)

1. One schema: #138's flat rules + `id`, `meta`, `meters`, `defaults`, per-rule `meter` and
   tuplet fields, `variation`, `song`, `assertions`, a canonical serializer.
2. `DGEN` v2 references a style by UUID only; `DSTL` embeds user styles.
3. Storage: per-beat subdivision within 64 slots per bar; the capacity table and the
   unavailable combinations of §10.2.
4. Lossless subdivision switching (absolute time kept through micro) or refusal, never a merge.
5. The host-meter rules of §10.4, including loops and missing data. Playback never edits
   state or export.
