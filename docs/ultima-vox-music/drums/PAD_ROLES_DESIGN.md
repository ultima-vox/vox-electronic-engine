# VOX Drums: semantic pad roles (option B) — Architecture Guard proposal

Status: **design for Architecture Guard review; implementation follows after approval** in this
Draft PR's branch. Task order: issue #127 comment 6863921664, priority 3. Decision: option B
(PR #129 review, issue #128).

Baseline: `drm/production-candidate` @ `c7c29bbbd6101dfb5e0cb2418a071d11caeb15b2`.

This change does **not** modify PR #129. The Ambient lane fix there is independent. If both are
integrated, this work rebases onto the integrated generator.

## 1. Problem

The generator writes lanes by the default layout: lane 1 kick, 2 snare, 3 clap, 4 closed hat,
5 open hat, 6 tom, 7 rim, 8 cymbal, 9–16 percussion (`Generator.h`).

On a kit arranged differently, for example the kick on pad 2, the rhythm grammar holds but other
instruments play it. The state has no role information except the pad **model**. A model is not
a role: a pad with the model "perc" may hold a user's kick sample.

## 2. Constraints verified at `c7c29bb`

- **C1** `DPAD` decode accepts versions 1–2 only. A `DPAD` v3 would make `c7c29bb` **reject the
  whole project** and the kit presets (`KitStore::decode`).
- **C2** The factory manifest hashes `KitStore::encode (factoryKit (k))`. Changing the `DPAD`
  encoding changes the committed factory hashes (`factory-content.csv`).
- **C3** Unknown sections are kept byte for byte by `SuiteProcessor` (projects).
- **C4** Kit and pad presets copy the `DPAD`, `DSMP` and `DPRM` sections (`Presets.cpp`).
- **C5** Pattern data is per lane, and lane n plays pad n. MIDI export, scheduler, lane locks and
  `DGEN` are lane-based.

## 3. Design

### 3.1 Role set

| Value | Role | Generator lane it replaces |
|---|---|---|
| 0 | none (the generator never writes this pad) | — |
| 1 | kick | 1 |
| 2 | snare | 2 |
| 3 | clap | 3 |
| 4 | closed hat | 4 |
| 5 | open hat | 5 |
| 6 | tom | 6 |
| 7 | rim | 7 |
| 8 | cymbal | 8 |
| 9 | percussion | 9–16, in pad order |

### 3.2 Persistence: a new section `DROL` v1, not `DPAD` v3

```
u32 version = 1
u8  explicit          (0 = derived default, 1 = set by the user)
u8  role[16]          (values 0–9)
```

The section is always 21 bytes.

- **Strict decode**, like the other musical sections: a wrong size or version, or a value above 9,
  rejects the restore. A role is musical data; it decides what generation writes.
- **Missing `DROL`** (every project and preset written before this change) gives the **identity
  roles** of §3.3. Generation is then byte-identical to today.
- **Old builds:**
  - Projects: `DROL` is kept and written back by `c7c29bb` (C3).
  - Presets: `c7c29bb` ignores the extra section, so roles are lost when it re-saves a preset.
    That is acceptable; nothing else changes.
- **Factory kits:** no `DROL` in the factory tables, so the identity roles apply. The manifest
  and hashes are unchanged (C2).
- **Presets:** kit presets copy `DROL`. A pad preset carries the same 21-byte `DROL`. On load
  only the entry of the preset's source pad (its `PresetInfo.source`) is used, and it goes to the
  target pad. **Alternative for review:** pad presets do not carry roles (§7 Q3).
- `productStateReserve()` +64 bytes.

### 3.3 Defaults and the explicit action

- **Identity roles** (default and factory): lanes 1–8 take roles 1–8; lanes 9–16 take
  percussion.
- **"Set roles from pad models"** is an explicit user action in the Inspector or Sound Editor
  role menu. Never automatic. It maps models as follows:

  | Model | Role |
  |---|---|
  | kick | kick |
  | snare | snare |
  | clap | clap |
  | tom | tom |
  | rim | rim |
  | cymbal | cymbal |
  | perc | percussion |
  | hat | closed or open hat (see below) |

  For each hat-model pad: note 42 or 44 → closed hat, note 46 → open hat. If the notes do not
  decide, the shorter decay is closed and the longer one open. If it is still ambiguous, the
  role is "closed hat" and the UI flags it.
- **Undo:** setting a role is an undoable kit edit.
- **Pad moves:** if a future feature moves a pad, its role moves with it.

### 3.4 Generator mapping (message thread, generation only)

- **Role map.** Before generating, the processor builds a map from the current roles:
  generator role slot (kick … cymbal, perc #1 … #8) → pad lane.
  - The first pad, in pad order, with a role fills that role's slot.
  - Percussion slots are filled in pad order.
  - With identity roles the map is the identity, and the output is byte-identical (tested).
- **Writes.** The style rules (`styleHit`, `fillLane`, variation decor mask, fill thinning,
  song break-slot kick removal) work on role slots. Results are written to the mapped lanes.
- **Lanes that receive nothing:** a lane whose role is none, or a second pad with an
  already-filled role, keeps today's behaviour for an unused lane: it is **cleared unless
  locked**.
  - **Alternative for review:** leave such lanes untouched (§7 Q2). This differs from today, so
    it is not the default.
- **Missing role:** a style needs a role (e.g. closed hat) that no pad has. Those hits are not
  written. The Generator panel lists "No closed-hat pad: hats not generated". No substitution.
- **Duplicate role:** two pads with the same core role. The first is used, and the panel lists
  "Two kick pads: pad 1 used".
- **Unchanged:**
  - Locks: lane locks and step locks stay lane-based (C5).
  - `mutateLanes`: the mask selects lanes, and the selected lanes are regenerated with the
    role their pad has.
  - Formats: `DGEN`, pattern and song formats, MIDI export, scheduler, host parameters.
  - Real-time path: no audio-thread change.

### 3.5 UI (inside the approved UI v3, no new screens)

- **Role selector:** Inspector (pad) and the Sound Editor Source section get a "Role" dropdown
  with the 10 values. The Pad Rack List can show the role as a short tag.
- **Generator panel:** shows the role map summary and the missing/duplicate notes from §3.4.

## 4. Migration and compatibility table

| Input | Roles used | Generator output |
|---|---|---|
| Project or preset without `DROL` (all existing content) | identity | byte-identical to today |
| Factory kit | identity | byte-identical; factory hashes unchanged |
| `DROL` explicit = 0 (written as derived) | identity | identical |
| `DROL` explicit = 1 | as stored | mapped (§3.4) |
| `DROL` malformed | — | restore rejected, live state unchanged (as DSNG/DGEN) |
| New project opened in `c7c29bb` | `DROL` kept, unused | old behaviour; roles survive the round trip |

## 5. Tests (in the implementation PR)

- **Codec:**
  - **R1** `DROL` round trip.
  - **R2** Every malformed variant rejects the restore with no live change.
  - **R3** A missing `DROL` gives identity roles.
- **Identity parity:**
  - **R4** The full `suite_drums_generator_style_tests` matrix, the factory manifest hashes and
    the candidate generator tests are unchanged byte for byte.
- **Mapping:**
  - **R5** Swapped kick and snare pads with explicit roles: four-on-the-floor on pad 2 and
    back-beat on pad 1, for all four-on-the-floor styles. The grammar test runs through the role
    map.
  - **R6** Hats moved (closed on pad 9, open on pad 12): hat grammar on those lanes.
  - **R7** Missing role: no hits for that role, the notice text is present, and the other roles
    are unchanged.
  - **R8** Duplicate kick role: the first pad is used and the notice is present.
  - **R9** Percussion ordinals follow pad order.
  - **R10** A role of none: the lane is cleared unless locked (or untouched, per Q2).
- **Locks and actions:**
  - **R11** Lane and step locks with remapped roles: locked cells unchanged.
  - **R12** Mutate Lanes with remapped roles: only the selected lanes change.
  - **R13** Fill, Variation and Song (break slot) use the mapped kick lane.
- **Determinism and parity:**
  - **R14** The same roles, settings and seed give the same output.
  - **R15** MIDI export parity of a remapped pattern (notes come from the pads' notes,
    unchanged).
- **Presets and old builds:**
  - **R16** A kit preset carries roles. A pad preset follows Q3. Presets without `DROL` load
    identity roles.
  - **R17** A project written by the new build and decoded with the `c7c29bb` rules: accepted,
    `DROL` kept as unknown.
- **Set roles from pad models:**
  - **R18** Each factory kit gives roles consistent with its models.
  - **R19** Hat notes 42/46, then decay, then ambiguity flagged.

## 6. What this does not do

- It does not identify sounds from samples. Roles are user statements, so a role that matches
  a kick sample is still not proof of the audible result.
- It does not change the Ambient fix in #129, styles, rhythms, `DGEN` or presets beyond `DROL`.
- It does not auto-remap existing projects. Identity holds until the user sets roles.

## 7. Decisions requested from Architecture Guard

1. **Q1** New section `DROL` v1 instead of `DPAD` v3 (C1, C2).
2. **Q2** A lane with role none (or a duplicate): cleared, as today, or left untouched.
3. **Q3** Pad presets carry the pad's role, or not.
4. **Q4** "Set roles from pad models" as an explicit action only; never automatic on load.
5. **Q5** Strict decode for `DROL` (reject the restore on a malformed section), like the other
   musical sections.
