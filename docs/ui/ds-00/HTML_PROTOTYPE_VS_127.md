# Interactive HTML prototype versus #127 and #135

Audit only. No DSP, no Drums/FX branch edits, no `main` edits, no new pictures, no token changes.

## What was found

Committed interactive HTML, one file:

| Item | Value |
|---|---|
| Branch | `design/vox-drums-hardware-reference` |
| Commit | `74513b1ce4e670d8126ccd78a8ce1b02a4764789` |
| Path | `docs/ultima-vox-music/drums/ux-v3/hardware-reference.html` |
| Blob | `9e483a682c706eca5edf933a8686e422d63a5027` |
| Companion | `HARDWARE_REFERENCE.md` blob `a8ecd924a05e158df9897e33455ef1b05a3da1de` |

Searched for `*.html` on `design/vox-drums-ux-v3`, `design/vox-drums-ux-v2`, `design/vox-drums-hardware-ui-reference`, and this docs branch. No other HTML file is committed.

`design/vox-drums-ux-v3` @ `44841270110ada36a7e0791b538907211e971186` has `source/gen.py` (blob `f9506a2e851befdb7ac24cfec2923c860748d86f`). It writes a static HTML string for screenshots. The file contains no `addEventListener`, no `<script`, and no `localStorage`. Generated `source/html` is not in git. The PNG screens are an architecture reference, not this interactive prototype.

The rejected neon sheet is not in the repository as HTML. Its magenta/violet accents are not in `hardware-reference.html` (no `#FF2D9D`).

## How to read the table

`reuse` — behaviour worth keeping when a native editor is eventually built.
`do-not-copy` — illustration, unwired control, or a colour/label that must not become the product.
The HTML's own banner says the sound engine is non-functional and values must be replaced. #135 says the same. Neither file is a ParameterID source. Drums IDs stay at `ef13daa3887dbd6079054384b04be7b65849d9ef`.

## Comparison

| #127 requirement | Interactive HTML | Verdict | Reuse |
|---|---|---|---|
| Not eight primary pages | Comment and layout are one workspace. No PERFORMANCE/SOUND/… tab strip. | match | yes, as layout intent |
| Named layouts: Beatmaking, Composition, Sound Design, Mixing | Click cycles Beatmaking → Sound Design → Mixing only. No Composition. Not saved. | partial | preset names, not the cycle |
| Dock: move panels, reject illegal drops | Grip characters are not drag sources. `addEventListener` is only workspace, mixer mode, Grid, List (4 listeners). | absent | no |
| One or two central editors | Sound workspace shows Full Sound Editor and moves the sample block to a second panel. No user split control. Generator is a tab that does not switch. | partial | sound + sample pairing |
| Pad Rack Grid 4×4 and List 16 | Both presentations exist. Grid/List buttons work. | match | yes |
| Select without sound; audition is a separate gesture | Hint text says face = audition and name row = select. Pad DOM is built once. No click handler on face or name. | partial | hint only; behaviour is not implemented |
| Mute/Solo on the pad | `M` and `S` buttons are drawn. No handlers. | illustrative-only | placement, not the dummy buttons |
| Inspector follows selection; Pin locks it | Right pane content is hard-coded per workspace. Sound workspace says "Pinned" with no pin state. Pin button has no listener. | diverge | pane role only |
| Bottom mixer, Collapsed / Compact / Expanded, never covers the grid | Mode buttons work. Expanded grows the row instead of overlaying. | match | yes |
| 16 vertical faders | CSS `::after` caps. Not `<input>`. Compact and expanded show 8 names plus Main. Expanded adds Delay and Reverb strips. | diverge | bottom placement only |
| Kit Level / All outputs, separate from Main meter | Strip title is `Main`. A note inside it says `Kit Level · all outputs`. Value text is always `Main L/R · OVER`. | diverge | the note, not the title or the OVER string |
| OVER latch, no CLIP badge | Disclaimer says truthful OVER, not fabricated CLIP. The Main strip always prints OVER. No latch, no reset. | diverge | disclaimer only |
| Hit flash vs measured peak vs velocity | Hint says the right indicator is peak, not velocity. The bar is a static CSS gradient. | illustrative-only | sentence in the hint |
| Read-only host transport | Text is `Host Playing` and `145.0 BPM` plus `from host`. No play button. The numbers are fixed. | partial | "from host"; do not keep 145.0 or "Playing" as if live |
| Step sequencer, 16 lanes, parameter lanes | Static hits. Velocity lane is closed-hat only. Microtiming marks are not a bipolar control. | illustrative-only | lane placement |
| Song: Edit Source Pattern / Edit This Bar | Pattern/Song buttons are not wired. No segmented edit target. | absent | no |
| Generator dock beside the sequencer | Not a dock. The Generator tab does not open a panel. | absent | no |
| Library of real kits | Rows: `FACTORY · Psy Full-on`, `House / Techno`, `Hip-hop Breaks`. Not loaded from factory-content. | do-not-copy | tab names Kits/Pads/Patterns/Songs only |
| Routing of Main and Out 2–9 | Shown only in the Mixing workspace, 8 rows, every output label `Main`. | illustrative-only | no |
| Settings / measured diagnostics | Not a panel. | absent | no |
| Workspace persistence and Reset | No `localStorage`. No Reset. | absent | no |
| No Insert-to-track | Not present. | match | yes |
| Drag MIDI | Button labelled `Drag MIDI`. No drag listener. | illustrative-only | label may stay; the button must not pretend a drop succeeded |
| Responsive 1040×700, banks of 8 | `min-width: 1020px`. Under 1180px, strips from the 8th child are `display:none`, not a 1–8 / 9–16 bank. | partial | do not copy the hide rule |
| Hardware material, restrained light | Matte metal/rubber drawing, orange action, aqua activity. Also pink, purple, and blue pad colours. | partial | material idea; not the hex list |
| Same semantic tokens as `Tokens.h` | HTML `:root` uses `#070d13`, `#fa8549`, `#3ce4d4`, `#ff6969`. Tokens are `#06121D`, `#00DDF5`, `#E4425D`. | diverge | do not replace tokens from this file |
| Real ParameterIDs | No `drm.*` strings. Levels such as `85%` and `380 ms` are literals. | do-not-copy | no |

## Colour ruling

TL: do not move neon colours into the next interface automatically.

Checked:

- Neon sheet accents (`#FF2D9D` and the violet/amber set from that sheet) are not in this HTML.
- This HTML is also not `Tokens.h`. Importing `#fa8549` / `#3ce4d4` / `#070d13` would be a second unapproved palette.
- This audit does not edit `Tokens.h`.

## What a later native pass may take from the HTML

Only after visual sign-off, and only as behaviour:

- one workspace, not eight pages
- Grid/List in one Pad Rack
- separate select and audition hit areas (the hint; the clicks still have to be written)
- mixer mode switch that resizes the centre instead of covering it
- Kit Level wording, once it is not inside a strip titled Main
- sound editor as Source → Tone → Filter → Output, sample editor as the second central slot

It may not take: dummy kit names, fixed BPM, always-on OVER, CSS faders, eight-strip mixer, unwired Pin, or any hex from the file.
