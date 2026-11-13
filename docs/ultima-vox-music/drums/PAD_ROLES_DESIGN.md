# VOX Drums: semantic pad roles (option B) — Architecture Guard proposal

Status: **revision 2, after Architecture Guard review 6864585514** on PR #132. That review
approved the direction and decided Q1, Q2, Q4 and Q5, and Q3 with conditions (§7). This revision
specifies pad-preset mapping (P1), selected-lane-only Mutate (P1) and the model-role preview
(P2). Implementation still waits for re-review, in this Draft PR's branch. Task order: issue #127 comment 6863921664, priority 3. Decision: option B
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
- **Kit presets** replace the whole kit, so they replace the roles.
  - **With `DROL`:** the roles become the preset's roles.
  - **Without `DROL`** (legacy, factory): the roles become the identity, because a legacy kit
    *is* the default layout. When this replaces explicit roles, the Library status line says
    "Roles set to the default layout of this kit".
  - **Undo Load** restores the previous roles with the previous kit.
- **Pad presets** (source pad N in the preset, loaded into target pad M):
  - **Without `DROL`** (legacy): the **target pad keeps its role**.
  - **With `DROL`:** loading still keeps the target's role. Only entry N is read. If it differs
    from M's role, the Inspector shows "Preset role: Kick — Apply". The role changes only on
    that explicit click, which is undoable. Other pads' roles are never read or changed.
  - **Saving** a pad preset writes `DROL` only when the kit has explicit roles. Entry N holds
    the pad's role; the other 15 entries are none and ignored on load.
- **Written only when explicit.** `DROL` is saved only after the user has set roles
  (explicit = 1). A project or preset that never had explicit roles saves without `DROL`, so
  existing projects and presets round-trip with the same sections as today.
- **Budget:** no change to `productStateReserve()`. 37 bytes fit the existing headroom; this
  is checked by boundary fixture T21 in the DUIS contract (PR #130).

### 3.3 Defaults and the explicit action

- **Identity roles** (default and factory): lanes 1–8 take roles 1–8; lanes 9–16 take
  percussion.
- **"Set roles from pad models…"** is an explicit user action in the Inspector or Sound Editor
  role menu. Never automatic, and never on load.
  - It opens a **preview**: 16 rows with pad, model, note, current role and proposed role.
  - Rows that need a decision are marked "?" and must be chosen before **Apply**. Nothing is
    written until Apply; Cancel changes nothing.
  - Apply is one undoable edit.

  Proposed roles by model:

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

  For each hat-model pad, the closed/open proposal is only a hint shown in the preview:
  - note 42 or 44 → closed hat;
  - note 46 → open hat;
  - otherwise, the shorter decay is closed and the longer one open.

  If two hat pads still tie, or a custom sample could make the proposal wrong, the row is "?"
  and the user must choose. A role is **never silently rewritten**.
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
- **Full generation (Generate Pattern, Variation, Song):** the result is built in a scratch
  pattern S.
  - Mapped lanes get their role's content.
  - Lanes with role none, or the second pad of a duplicate role, get nothing.
  - Every unlocked cell of every lane takes S (unmapped lanes are therefore **cleared**,
    decision Q2).
  - Locked steps and locked lanes are kept.
- **Missing role:** a style needs a role (e.g. closed hat) that no pad has. Those hits are not
  written. The Generator panel lists "No closed-hat pad: hats not generated". No substitution.
- **Duplicate role:** two pads with the same core role. The first is used, and the panel lists
  "Two kick pads: pad 1 used".
- **Unchanged:**
  - Locks: lane locks and step locks stay lane-based (C5).
  - **Mutate Lanes (physical lanes only, decision Q2/P1):**
    - The mask M is the set of **physical lanes** the user selected. It is never translated
      through the default layout.
    - S is built exactly as for full generation, from the current roles.
    - Then, for each lane L in M only, every unlocked cell of L takes S's lane L. A selected
      lane whose role is none or a duplicate is therefore cleared, unless locked.
    - Lanes not in M are **not touched**, even when they are the mapped lane of a role.
    - M = 0 changes nothing.
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
  - **R10** Full generation: lanes with role none or duplicate are cleared, except locked cells.
- **Locks and actions:**
  - **R11** Lane and step locks with remapped roles: locked cells unchanged.
  - **R12** Mutate Lanes, all 7 styles. Role sets: reordered, duplicate, missing, with none
    roles, and identity. Masks: 0, one lane, two lanes, all. Two locked steps. Checks: every
    unselected lane is byte-identical; selected lanes equal S except locked cells; mask 0 is a
    no-op; deterministic. Plus a negative test showing that the default-layout mask is never
    used (selecting physical lane 1 when kick is on pad 2 changes only lane 1).
  - **R13** Fill, Variation and Song (break slot) use the mapped kick lane.
- **Determinism and parity:**
  - **R14** The same roles, settings and seed give the same output.
  - **R15** MIDI export parity of a remapped pattern (notes come from the pads' notes,
    unchanged).
- **Presets and old builds:**
  - **R16a** A kit preset with `DROL` replaces the roles. A legacy kit preset sets identity roles
    and shows the notice. Undo Load restores the previous roles.
  - **R16b** A legacy pad preset (no `DROL`) loaded from source N into target M ≠ N: M keeps its
    role, and no other role changes.
  - **R16c** A modern pad preset from N into M: M keeps its role on load. The "Apply" offer
    appears when the roles differ. Apply sets only M; undo and redo restore and reapply it.
  - **R16d** A project without explicit roles saves without `DROL`: the same section set as
    today. Factory manifest hashes are unchanged.
  - **R17** A project written by the new build and decoded with the `c7c29bb` rules: accepted,
    `DROL` kept as unknown.
- **Set roles from pad models:**
  - **R18** The preview proposes, for each factory kit, roles consistent with its models.
    Nothing is written before Apply; Cancel is a no-op.
  - **R19** Hat proposals by note 42/44/46, then decay. A tie or doubt is marked "?" and blocks
    Apply until chosen. Apply is one undo step.

## 6. What this does not do

- It does not identify sounds from samples. Roles are user statements, so a role that matches
  a kick sample is still not proof of the audible result.
- It does not change the Ambient fix in #129, styles, rhythms, `DGEN` or presets beyond `DROL`.
- It does not auto-remap existing projects. Identity holds until the user sets roles.

## 7. Architecture Guard decisions (review 6864585514)

1. **Q1** `DROL` v1 instead of `DPAD` v3 — **approved**.
2. **Q2** Full generation clears unlocked unmapped or duplicate-role lanes and keeps locked cells
   — **approved**. Mutate Lanes touches only the selected physical lanes (§3.4).
3. **Q3** Pad presets carry the role — **approved with conditions**, now specified in §3.2:
   the target keeps its role; Apply is explicit; legacy presets keep the target role.
4. **Q4** Model-based roles are an explicit action only — **approved**. Now a preview with
   confirmation (§3.3).
5. **Q5** Strict decode — **approved**.

Requested: re-review of this revision before implementation starts.
