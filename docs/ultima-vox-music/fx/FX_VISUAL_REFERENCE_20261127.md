# VOX FX — owner-approved visual reference identification (TL 7240861728)

TL directive (P0 owner visual rejection, fd57112): «интерфейс не тот который
утверждали». Locate the actual owner-approved FX visual source from prior
design assets/issues/branches, record exact path/SHA; if ambiguous, present
the candidate reference to TL/owner for identification **before another
rewrite**. This document is that record; the identification question is
escalated with the owner in the working session before any further UI work.

## 1. Located approval chain (exact paths, SHAs, quotes)

**Gate 1 — owner visual acceptance (2026-11-18), `issues/comments/6980638479` on PR #127:**

> «Owner visual Gate 1 — ACCEPTED as current design baseline (2026-11-18)»
> Owner reviewed the public VOX **Drums** Workbench
> (https://ultima-vox.github.io/vox-electronic-engine/#drums) and decided:
> **«да вроде нормально, пока оставляем так»**.
> "The existing #127 dockable workspace and #135 hardware-inspired styling
> are the accepted implementation reference."
> "Authorize the native VOX Drums UI implementation within integration #141
> against #127/#135 and the #160 JUCE porting map. No WebView, no legacy
> eight-page shell."

**TL reference freeze (2026-11-18), PR #160 issue comment ("TL ownership handoff — browser work complete, reference frozen"):**

> "**Gate 1 accepted the current visual language in #127.**"
> "This #160 branch at `e420d2c0fa95cebf4ec0ff0c4b359d9d35c28d93` remains a
> **read-only design reference** for implementation."

**FX-specific reference — PR #160 head `e420d2c0fa95cebf4ec0ff0c4b359d9d35c28d93`:**

- `apps/vox-music-prototype/src/fx-prototype/FxPrototype.tsx` — FX shell: product header, zone tabs, A/B compare, undo/redo
- `apps/vox-music-prototype/src/fx-prototype/Zones.tsx` — per-zone composition (Perform/Design/Timeline/Process/Trigger/Presets/Advanced)
- `apps/vox-music-prototype/src/fx-prototype/fx.css` — FX grids and wells
- `apps/vox-music-prototype/src/design-system/system.css` + `ui.tsx` + `controls.tsx` + `tokens.ts` — module/material language shared with the Gate-1-accepted workbench
- `apps/vox-music-prototype/docs/HANDOFF.md` — "browser reference candidate for the single owner visual review"

**Residual ambiguity (why this is escalated):** the Gate-1 owner verdict was
given on the **Drums** workbench screen; the FX screens implement the same
design system but were never individually signed off by the owner (the TL
handoff says #160 is "an available FX-specific reference, not proof that
owner separately signed off every FX screen"). The candidate therefore is:
**the frozen #160 FX prototype composition (`e420d2c0…`) as the concrete FX
reference, which carries the Gate-1-accepted visual language.**

## 2. Extracted reference composition (verbatim from `Zones.tsx` @ `e420d2c0`)

**Shell (`FxPrototype.tsx`):** product header — raised icon square + eyebrow
`INSTRUMENT 02 / EVENT DESIGN` + **H1 `VOX FX`**; right side: eyebrow
`Program` + **strong program name**, `DSP NOT CONNECTED` dot badge, Undo/Redo
icon buttons. Below: zone tabs (`fx-nav`). Content scrolls inside the page.

**Module anatomy (`design-system/ui.tsx` + `system.css`):** `ds-panel` — 1 px
`--line` border, 10 px radius, gradient background `160deg, #1b1e26 →
#181b22 45%`, surface shadow; **header row min-height 42 px** (gradient
`#222630 → #1e222a`, bottom border) = icon + title (650 13 px display font) +
spacer + **status badge** (9 px mono, bordered well chip, tone accent when
active). Wells inside panels: `#151820`, 1 px `--line-soft`, 6 px radius.

**Tokens (FX theme):** background `#101218`, panel `#181b22`, raised
`#22262f`, well `#11141a`, line `#313640`/soft `#292e37`, text `#eef0f3`,
secondary `#adb4c0`, muted `#929aa9`, success `#89d4aa`, warning `#f3bd72`,
error `#fb9995`, **FX accent `#7ce2d0`** (on-accent `#112627`), modulation
`#b3a7f4`; fonts display *Space Grotesk*, mono *IBM Plex Mono*, body *Inter*;
eyebrow = 10 px mono uppercase, 0.085 em tracking; spacing 4 px rhythm;
radii 4/6/9/10 px. Piano keys (Trigger): light gradient `#b1b6c1 → #e8e9ed`,
inset key shadow, rounded bottoms, 9 px mono labels.

**PerformZone — grid `1.22fr / 1fr`, macros module spans both columns:**
1. Module **"Program Generator"** (badge `SEEDED`): grouped row Category /
   Style / Pitch key (1.1fr / 1fr / 0.63fr); row `Seed` numeric + `Length`
   segmented Seconds|Bars + numeric; **well of four recipe dials**
   (Intensity, Complexity, Energy, Stereo motion); row `Keep on generate` +
   lock toggles; primary button **"Generate program"** + caption "Creates
   editable Program data. No audio render."
2. Module **"Event Preview"** (badge `NO DSP`): `Selected Program` eyebrow +
   large program name (display font up to 28 px) + category/style caption;
   event chart with accent position line + meter; buttons "Preview event" +
   "Clear events".
3. Module **"Performance Macros"** (badge `08 MACROS`, full width): well with
   8 macro knobs in one row, named `M1 · Intensity … M8 · Width`.

**DesignZone:** 4-column grid of layer Modules `Layer A…D` (badge
`IN PROGRAM`/`INACTIVE`), each with family select, 3-column dial grid
(9 px labels), 2×2 curve buttons (33 px graphs) + curve directory.

**TimelineZone:** Module **"Event Timeline"** (badge `20 EDITABLE CURVES`)
with 5-column curve directory (2-col button groups in wells, active border).

**ProcessZone:** Module **"Signal Flow"** (badge `CONFIGURATION ONLY`) with
an A–D SUM → 01–04 → MASTER badge track; per-slot Modules `FX Slot N`
(badge `BYPASS`/`OFF`/`IN ROUTE`); Module **"Output & Tails"** with master
fader + notice.

**TriggerZone:** Module **"Trigger Pattern"** (badge `120 BPM · LOCAL 4/4`)
and Module **"Trigger Keyboard"** (badge `MIDI I/O NOT CONNECTED`) with the
piano keyboard + active event list.

**PresetsZone:** grid `1.25fr / 1fr` browser; **AdvancedZone:** Module
"Instance Diagnostics" (warning badge, 4-column stat wells) + Module
"Quality & Safety".

## 3. Gap map — fd57112 native UI vs reference (owner complaints)

| Owner/TL observation | Reference answer |
| --- | --- |
| Giant gray stock-looking combo boxes | Reference selects are compact dark wells with mono labels, grouped in one row; native must use the VoxComboBox styled as a well field, not stock JUCE look |
| Tiny isolated knobs | Recipe dials live in a labelled 4-column well; 8 macros in a full-width well row with names |
| Full-width Generate/Mutate strips | Reference: one primary "Generate program" button inside the generator module with caption; no full-width strips |
| Vast empty lower workspace | Perform is a dense 2-column module grid; every module fills its column with wells/groups |
| Almost absent product header/identity | Reference shell: icon + `INSTRUMENT 02 / EVENT DESIGN` eyebrow + `VOX FX` display title + program name + undo/redo |
| Flat generic controls | Module headers with badges, gradient panels, wells, mono eyebrows — the Gate-1 material language |

## 4. Port plan (after identification is confirmed — no new design brainstorming)

Rebuild `FxPanel` composition to the extracted reference, native JUCE only:
1. **Product header** (`VOX FX`, eyebrow, program name, undo/redo) above the
   workspace tabs; persistent playable keyboard stays (owner's current request).
2. **Module component** in `libs/vox-ui` (panel gradient + 42 px header +
   badge) shared with Codex's contract if applicable, else FX-local.
3. PERFORM → Program Generator / Event Preview / Performance Macros modules
   with the exact groupings above (all real bindings preserved: seed,
   category/style/key→family/bars/duration, 4 recipe dials, locks row,
   Generate/Mutate, preview trigger, `fx.macro.1..8` knobs).
4. DESIGN → 4 layer module cards with badges and 3-column dials + 2×2 curves;
   TIMELINE → Event Timeline module with 20-target directory; PROCESS →
   Signal Flow track + per-slot modules + Output & Tails; TRIGGER → pattern +
   keyboard modules; PRESETS → browser grid; ADVANCED → diagnostics wells.
5. Materials: FX accent `#7ce2d0`, tokens above, mono eyebrows/badges, well
   groupings — through `libs/vox-ui` Tokens/Typography (no WebView).
6. Evidence: **one side-by-side** — reference (live Pages `#fx` or
   reconstructed-from-source render, labelled) vs native PERFORM and DESIGN
   at the same viewport — plus single renders of the remaining workspaces
   (not 128 variants).

Functional keyboard/ownership, bindings, gain work and all tests from
`651da82…fd57112` are preserved untouched; only the visual composition ports.
