# VOX Drums generator: style fidelity audit (issue #128)

Baseline: `drm/production-candidate` @ `c7c29bbbd6101dfb5e0cb2418a071d11caeb15b2` (PR #125, not
modified). Work branch: `drm/genre-fidelity`.

What this audit proves and what it does not:

- It proves, on event grids, that every named style keeps the rhythm rules written in
  `Generator.cpp`. This holds for Pattern, Variation B, Fill and generated Songs, over a settings
  matrix. It also proves that the same input gives the same output.
- It checks which instrument actually plays each generated lane on the three factory kits.
- It does **not** prove musical quality or genre authenticity. That needs a listening review (§6).
- No audio was rendered and no DAW was used.

## 1. Rhythm grammar of each style (as implemented)

Positions are 16ths of a 4/4 bar (1–16). "Always" = in every bar and for every setting of the
matrix, except in a fill (the last 2–8 sixteenths of the last bar), where hats and percussion may
make room for the fill. Kick and back-beat lanes are never removed by a fill.

| Style | Kick | Back-beat | Hats | Secondary / ornament |
|---|---|---|---|---|
| Full-on / Psy | always 1, 5, 9, 13; never elsewhere | clap on 5 and 13 when density > 0.15; snare on 13 by chance | open hat always on the off-beat 8ths (3, 7, 11, 15); closed hat on the 2nd and 4th 16th of each beat by density, on the beat only when density > 0.75; never on the off-beat | perc 1 on the 4th 16th of a beat, perc 2 on the 2nd, rim on 8 and 16 (syncopation, complexity) |
| Goa | always 1, 5, 9, 13; never elsewhere | snare always on 13, on 5 when density > 0.5 | closed hat always on the off-beat 8ths; ghost 16ths by density | perc 1 on every 3rd 16th off the beat (3-against-4 line), perc 2 on 3, 6, 9, 12, 15; tom on 11, 12, 15; open hat on 15 |
| House | always 1, 5, 9, 13; pickup on 16 by syncopation | clap always on 5 and 13 | open hat always on the off-beat 8ths; closed hat on 1/5/9/13 when density > 0.3; ghosts | rim 4, 7, 12; perc 1 8, 11, 14; snare ghost 16 |
| Techno | always 1, 5, 9, 13; never elsewhere | clap on 13 (density > 0.25), on 5 (density > 0.6) | closed hat always on every 8th, 16ths when density > 0.35, off-beat accented | tom rumble off the beat; rim 4, 10, 15; perc 1; open hat 15; cymbal on bar 1 |
| Trap | always 1; never 5, 9, 13 (half-time) | snare always on 9; never on 5/13 outside a fill | closed hat on every 8th with ratchet rolls by complexity | kick ghosts on 7, 8, 11, 14; open hat 7, 15; perc 1 4, 12 |
| Breaks | always 1 and 11; never 5, 9, 13 | snare always on 5 and 13; ghosts on 8, 10, 16 | closed hat on every 8th, 16th ghosts | kick ghosts 4, 7, 14; open hat 7; rim 3, 12; cymbal bar 1 |
| Ambient | always 1; otherwise only 11; never anything else | none (no snare, no clap) | closed hat ghosts on the off-beat 8ths | rim ghosts 5 and 13, cymbal on odd bars, perc 1–2 ghosts; every non-kick hit has probability < 100 % |

Measured distinctions, from the hit table in `report.md`:

- **Goa vs Full-on:** on the off-beat Goa uses the closed hat and Full-on the open hat. Goa adds the
  3-against-4 perc 1 line. Goa's beat positions carry no hats; Full-on's carry them in about 20 %
  of bars.
- **Techno vs Full-on:** Techno has closed hats on every 8th, including the beats. Its clap is
  sparse.

Fills use the style's own instruments: Full-on/Goa snare–tom, House clap–snare, Techno tom–rim,
Trap hat–snare with 4× ratchets, Breaks snare–tom, Ambient perc–cymbal.

Variation B keeps A's kick, snare and clap. It regenerates the other lanes in the same style with a
derived seed and ends with a fill.

The Song uses one style for all five slots: intro (density × 0.45), main, lift, break (no kick) and
peak.

## 2. Method

`tests/suite/DrumsGeneratorStyleTests.cpp` (`suite_drums_generator_style_tests`, registered in
CTest):

- **Matrix, per style:** 12 seeds × density {0, .25, .5, .75, 1} × complexity {0, .5, 1} ×
  syncopation {0, .5, 1} × fills {0, .5, 1} × variation {.4, 1} × grids {16×4, 64×4, 16×1, 32×2}.
  That is 12,960 patterns plus 12,960 B variations per style, with swing 0.25 and humanize 0.3.
- **Songs:** 6 seeds × density {.2, .6, 1} × 16/32/64 bars. All five slots are checked, A and B;
  the break slot must contain no kick.
- **Fills:** a fill written into bar 2 of a groove, 20 seeds × fills {0, .5, 1}.
- **Assertions** (§1 "always/never"):
  - kick grid;
  - back-beat;
  - hat grid outside fill windows;
  - no hit between 16ths on 32/64-step grids;
  - reproducibility: a pattern pre-filled with random content gives the same result as an empty
    one (no locks).
- **Roles:** for each factory kit and style, the model of the pad on every lane that receives
  hits. A *core mismatch* means the lane's role and its pad model are different core instruments
  (kick, snare, clap). Core mismatches fail the test. A pass means the factory pad **models**
  match the roles. It does not prove the audible roles: a pad's sample or layer can sound
  unlike its model.
- **Mutate Lanes:** 630 cases.
  - Matrix: 7 styles × 6 seeds × lane masks {none, closed hat, open hat + perc 1,
    kick + snare, all} × lane locks {none, snare, all}.
  - The baseline is a pattern in another style and seed, with two locked steps.
  - Checks: only the selected, unlocked lanes change; they equal the selected style's lanes;
    locked steps and lanes are untouched; mask 0 and a fully locked kit change nothing; equal
    input gives equal output.
- **MIDI round trip:** two generated patterns (Full-on, Trap) are exported, written to a file
  and read back with `juce::MidiFile`.
  - The file must be one track at 960 ticks per quarter.
  - Every Note On must match the exported note list (tick, note, velocity) on channel 10,
    within the 4 bars.
  - Each of the 21 listening examples is checked to have been written.

Run: `suite_drums_generator_style_tests <report.md> [<midi folder>]`. The report in this folder
was generated by it.

## 3. Results

**(a) Rhythm grammar: no violations.** All styles, all contexts: 185,640 checked patterns
(514,920 bars). See `report.md`.

**Reproducibility:** 1,680 pairs; none differ.

**(b) Audible roles:** a reproducible mismatch on the factory kits, before the fix (`c7c29bb`):

| Kit | Style | Lane | Generator role | Pad model | Hits per 1000 bars |
|---|---|---|---|---|---|
| House Techno | Ambient | 11 | percussion | **clap** | 1437 |
| Hip-hop Breaks | Ambient | 11 | percussion | **snare** | 1437 |
| Hip-hop Breaks | Ambient | 12 | percussion | **kick** | 1361 |

Ambient wrote percussion ghosts to lanes 9–12. Two factory kits hold a clap, snare or kick on
lanes 11–12. On the Hip-hop Breaks kit, Ambient therefore played about 1.4 extra kick hits and
1.4 snare hits per bar, on off-beat 16ths. This contradicts the style's own rule ("kick only on 1
and the and of 3, no back-beat instrument"). The rhythm grid was correct; the instrument was not.

All other styles write only lanes 1–10. Those lanes hold the same models on all three factory
kits: kick, snare, clap, hat, hat, tom, rim, cymbal, perc, perc.

## 4. Fix (generator-only, deterministic)

`Generator.cpp`, Ambient: percussion ghosts only on lanes 9–10 (perc 1, perc 2). Probabilities and
positions are unchanged. After the fix the role check finds no mismatch on any factory kit.

Unchanged:

- the other six styles (byte-identical output);
- Ambient's lanes 1–10;
- `GeneratorSettings`, DGEN, seeds, locks, undo, pattern/song format;
- MIDI scheduler and export, host parameters, buses;
- factory content, whose patterns use Full-on, Goa, House, Techno, Trap and Breaks only. The
  committed hashes are checked by `suite_drums_candidate_tests`.

Effect on users: a newly generated Ambient pattern has no hits on lanes 11–12. Existing projects
and stored patterns are not changed.

## 5. Limit: user kits arranged differently (not fixed; owner decision needed)

The generator writes lanes by the default layout and receives no kit information. If a user puts
the kick model on pad 2 and a snare on pad 1, Full-on still writes its four-on-the-floor to lane 1,
so the snare plays it. The rhythm grammar holds; the audible roles do not. The report records this
for Psy Full-on with pads 1 and 2 swapped.

The state holds no instrument-role field. The only role information is the pad **model** (kick,
snare, clap, hat, tom, rim, cymbal, perc). A user's own sample says nothing about its sound.

Proposal for owner / Architecture Guard approval (not implemented):

- **Option A — map roles by pad model, no format change.**
  - Generation takes the 16 pad models as input.
  - For each role, the default lane is kept when its model matches. Otherwise the first unused
    lane with that model is used. If no pad has that model, the role is not written and the UI
    says so ("no kick pad in this kit").
  - Closed vs open hat is not distinguishable by model: keep lanes 4/5 when both are hats;
    otherwise mark the role as ambiguous.
  - Factory kits map to the identity, so their output is bit-identical. Seeds, locks and undo are
    unchanged.
  - The Generator panel should show the mapping (e.g. "Kick → pad 2").
- **Option B — an explicit role per pad.** A new persistent field means a state format change and
  migration.

**Decision (tech lead, PR #129 review):** custom-kit roles are to be solved with **option B**,
explicit kit-persistent semantic roles. That work goes in a future dedicated Draft PR, not in
this focused fix.

No user MIDI notes are remapped in either option.

## 6. Listening review (owner)

`midi/<style>-seed<n>.mid`: 3 seeds × 7 styles, density 0.6, complexity 0.5, syncopation 0.4,
fills 0.5. Each is 16 steps × 4 bars on channel 10 with the factory note map. The tempo is set
for listening only: Full-on/Goa 145, House 126, Techno 130, Trap 140, Breaks 92, Ambient 90.

Requested: for each style, does the pattern read as that style? Answer yes / no / why. Automated
checks do not answer this question.
