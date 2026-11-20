# Native Drums workspace — development ledger

Codex owns Drums per [#141 / 6986368003](https://github.com/ultima-vox/vox-electronic-engine/pull/141#issuecomment-6986368003).
Continue `dev/drums-native-workspace`, Draft [#163](https://github.com/ultima-vox/vox-electronic-engine/pull/163),
target `integration/vox-drums-1.0@ef13daa3887dbd6079054384b04be7b65849d9ef`.
Current request: [TL 7038015054](https://github.com/ultima-vox/vox-electronic-engine/pull/163#issuecomment-7038015054),
updated 2026-11-20T11:09:26Z against parent `351aa0a9`.
Corrupt-layout budget policy: [TL 7048135242](https://github.com/ultima-vox/vox-electronic-engine/pull/163#issuecomment-7048135242), updated 2026-11-20T19:02:07Z.

**Development checkpoint, not merge/release acceptance. HOLD does not stop coding.**
Verify live HEAD and the PR's exact-source evidence before resuming.

## Production implementation

- `createEditor()` uses `DrumsWorkspace`. The unpublished eight-page panel remains an
  explicitly named donor test fixture. Its functional regressions still run; native
  production tests independently write/read all 174 frozen host bindings and reopen
  the actual production editor. No DSP, plugin/FUID/automation identity changes.
- One physical 16-pad Rack has separate rubber HIT and SEL controls, Grid/List and real
  kit labels. HIT uses the bounded audition queue at velocity 110 without selection.
  Hit telemetry is binary, never invented MIDI velocity. One 30 Hz consumer distributes
  a snapshot to the Rack and the single bottom Mixer.
- Mixer: 16 physical level/pan/send/mute/solo strips, requested-output/fallback labels,
  real pad/Main peaks, nine outputs, Kit Level's all-outputs/new-triggers meaning,
  one Pads/Returns viewport, all FX returns, swing/accent and all 16 lane enables.
  Drums-local metallic 52/34 px dials/fader caps retain actual native input/units/skew.
- Sound/Sample and donor controls share canonical bindings. Sample A/B, async Load,
  Clear/Library/drop and waveform trim use the existing sample engine. Active gestures
  retain pad/player identity; missing files and closing before completion are tested.
- Real Generator/roles/Library/Routing/Diagnostics views preserve role preview, explicit
  Apply/Cancel, stale-proposal/duplicate refusal and one-step Undo. Embedded custom
  definitions display correctly; choosing a factory clears the override. No note remap.
- Side panels move between docks, tabs and up to three stacked groups, hide/reopen and
  collapse to 26 px rails. Views are reparented, never copied. Empty sides disappear.
  Central editors remain unique, support splits/focus; Mixer heights/bank persist.
  Divider gestures commit only on release. Narrow fallback never rewrites saved intent.
- Narrow Step/Song toolbars wrap. Only the dense Step timeline scrolls horizontally;
  Song uses responsive readable bar columns. Sound/Sample height follows visible controls.
- Native Step has 0-2 graphical Velocity/Probability/Ratchet/Microtiming lanes from
  canonical DLAY record8. Ruler, pad cells and value bars share the same BarTiming x
  mapping and one timeline viewport. Paging pad rows never redirects/empties the
  selected pad's lane. Native Lanes menu persists presentation, not musical state.
  Real lane gestures reuse canonical cell setters and existing edit/Undo/Redo, one
  drag per history entry. Epoch/grid/scope/external-generation changes cancel stale
  gestures. Song remains linked until its first real edit; source patterns stay exact.
  At minimum size/compact Mixer, commands fit without outer Step scrolling; only
  dense timeline content scrolls. Actual native 1040x600 snapshot inspected.
- One context Inspector follows the selected step or Shift-click range, displays mixed
  values honestly and edits Velocity/Chance/Ratchet/Micro/On/Accent/Lock through the
  canonical pattern store and musical history. Native Step hides its duplicate donor
  numeric controls; the donor fixture retains them. One drag is one Undo entry.
  DLAY9 kind2 pins the exact slot/A-B/bar/pad/range, including restored B addresses;
  unavailable addresses disable edits and retain Unpin. Selection and Pin do not alter
  music. Each microtiming cell uses its own actual BarTiming duration.
  Generation-conditional writes refuse stale snapshots under the existing store lock;
  a restore fence prevents reads/edits of a partially applied project. Active gestures
  retain their target and abandon subsequent writes after external edits/restore.
  Pad/step Open Sound uses the Inspector's actual target and persistently reopens a
  hidden central editor, without audition or musical Undo. No new DLAY pin kinds,
  host parameters, DSP model or processBlock work.
- Native Step MIDI has fixed Pattern scope; Song has a direct Entire Song save/drag
  handle for 16/32/64 bars. Both reuse the frozen exporter, existing MidiDragButton
  click/drag state machine and unique per-instance/per-drag files. The native root
  supplies the real drag container. Native Song hides the donor global scope selector.

## Project, user and ephemeral state

- DLAY v1: little-endian, <=2048 bytes, <=32 strictly ascending unique records, each
  payload <=256 bytes; strict lengths, ranges, panel uniqueness and allowed zones.
  SELECTION is appended as type 11. Type 2 is unassigned/opaque, not a size owner.
  Unknown records survive committed edits. UIST remains the only 8-byte size record.
- Processor ownership uses the existing settings lock. Restore publishes UI generation
  and asynchronous notification after musical stores/parameters apply; editors unregister
  safely. No Component mutation from host/audio callbacks. Save during drag uses the
  last commit. Layout edits do not add musical Undo and mark non-parameter host state dirty.
- DUIS is recognised/discarded, never migrated/written/retained as unknown. Only the
  canonical three selection fields persist; donor page/export-scope is session-only.
- Invalid v1 DLAY shows default Beatmaking and retains raw bytes until edit/reset,
  except the authorised recovery described below. Future versions are opaque,
  not misclassified as corrupt v1 merely because they exceed v1 size. Accepted future
  bytes survive session edits; only an explicitly confirmed Reset replaces them.
- Future opaque data must fit alongside worst-case music and preserved unknown sections
  using the existing forward-safe VOXS budget. Excessive future input is refused
  transactionally, never silently discarded. VOXS hard cap remains 1 MiB.
- User workspaces: `userApplicationDataDirectory/ULTIMA VOX/VOX Drums/Workspaces/workspaces.json`;
  `VOX_DRUMS_WORKSPACES` overrides the folder. JSON v1, <=32 names of 1-64 characters,
  case-insensitive uniqueness, bounded file/base64/TLV input. Temp-file replacement,
  .bad quarantine, factory presets and Save/Delete/Open-panel menus. Applying copies
  DLAY into the project; projects never reference the user file.
- Preset Load preserves layout. Reproduced Undo Load rollback of later DLAY/UIST edits
  is fixed for all four musical preset types.
- Shared editor maximum is 2560x1600; minimum 1040x700 unchanged. The opt-in Drums
  `SuiteEditor::setPresentationSize` clamps to logical work area without writing UIST.
  Real resize clears the override. Constructor constraint setup no longer replaces
  requested size with minimum size. FX/Synth do not opt into presentation fallback.
  Shared changes were coordinated in #156: comments
  [7041795552](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7041795552) and
  [7043092478](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7043092478).
  FX/Synth branches and runner policy are untouched.

## Evidence and limits

- JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8` (9.0.1), GNU C++14.2/C++20.
  Codec/docking actions pass ASan/UBSan, including bounds, order, duplicate/misplaced
  panels, opaque/future data, move/hide/rail/group limits.
- Red/green regressions cover toolbar overflow, missing MIDI handles/container/scope,
  constructor UIST reset, unrelated-divider ratio rewriting and Undo Load layout rollback.
- Local increments: production 10/10 (267.02s); native docking 3/3 (15.19s); responsive/
  state/real-mouse 3/3 (64.79s); T15/T23 and all-three shells 5/5 (121.31s).
  Combined production/persistence/MIDI/future packet: **10/10 (287.08s)**, including
  real Song/Pattern drag files and visible failure, accepted future growth, old corpus,
  all 174 bindings and the three product shells. These are not Windows PASS.
- TL 7048135242 red/green: the CRC-valid fixture is 968105 bytes (the reported
  968095 bytes plus an opaque control section), with 900000 invalid-v1 DLAY bytes.
  It round-trips exactly without pressure. After legal maximum pattern-grid growth,
  recovery saves **170585 / 1048576 bytes**; reopen preserves every musical/opaque
  control section and UIST. CRC/music rejection, persistent diagnosis, repeated
  save/reopen and musical Undo/Redo pass in the production project-state test.
  Local focused codec/project tests: 2/2 PASS; codec ASan/UBSan PASS. Merged #153
  StyleEngine/StyleLibrary tests: 2/2 PASS (86.02s), retaining Windows-only cases for CI.
- T21: fixed encoder fixtures 602924 bytes + full DSTL upper bound 65584 =
  **668508 / 753664 bytes**, including UIST/container, full 2048-byte DLAY and DROL.
  The 736 KiB product reserve is unchanged.
- Graphical lanes regressions cover exact 64-cell alignment at 376/900/1280 widths,
  touched-cell Undo/Redo, canonical ranges/tick quantisation, no music/UIST change from
  presentation, header non-edit, external-edit/restore cancellation, pad-row paging,
  closed-editor/future DLAY retention, and Song unlink/source isolation. Local evidence
  is recorded per published SHA in the working PR.
- Captures use actual native content at 1040x600, 1280x620, 1440x800, 1600x900 and
  raster 1/1.25/1.5/2. They are **not OS/host DPI evidence**.
- Linux Debug emits inherited JUCE wake-queue assertions during bulk UI construction and
  static AudioFormatManager shutdown reports. Exit codes are recorded separately;
  do not call these logs warning-free or diagnose them as Windows DSP failures.
- Historical parent `351aa0a954d4b812304601b5c7dc4a9e9027be71`:
  [Windows suite 44717551921](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/44717551921),
  job 136232641327: inspected 34/34 serial +8/8 gain, validator 47/47 per product.
  Checkout `75b46a0128878b067a8d078546efbf19e7cd03c2` shares source tree
  `ca2077fc02c686f5b19edf25e5c0b166ea6249a3`; Drums binary SHA256
  `d47a27fae39b9b687426dc31fc64641c18155928edeaa62cb136825fe55d385b`.
  Artifacts 14623431762 (listening), 14622434085 (suite/native).
  **This historical result does not qualify later source.**
- Production/persistence head `13532de9fe637cd7fef74c51b75ba5ef24813868`: inspected
  [Windows suite 44767815227](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/44767815227),
  job136397182173: 36/36 serial +8/8 gain; validator47/47 each. Manifest checkout
  `91cc54b6bebc5159f985265b8956a01c723dd6ac`, same source tree `2a509af8e377ce1f0f38670afc2a7fc5e2938131` as the feature head.
  Drums binary SHA256 `72d16e7357d1b6ae16194fe06cdf06716dd77d184bf2e4ef11d50896a198eaee`.
  Listening artifact14647453738 digest `d900510e544e2d208efc937eb39e09de7ea107d52e518cb1f82eab83543f2dc1`;
  suite14648217271 digest `c64e58f3cdf3b77e44d0ad467b2d2ebe0c711e6d4c2a36f835caaf34d8099819`.
  **This parent result does not qualify the recovery fix.**

## Open gates

1. Verify the corrupt-v1 recovery fix on its exact published Windows head; the policy is
   resolved by TL 7048135242. Preserve invalid v1 raw exactly while the full state fits.
   If and only if a full save exceeds VOXS 1 MiB, prove that replacing only structurally
   invalid, explicitly version-1 DLAY with canonical defaults makes it encode. Preserve
   all music, UIST and other opaque records. A failed proof changes nothing. The same
   check applies to a fully validated restore plan if canonical musical encoding grows.
   CRC/framing/UI/music/unknown-section validation remains transactional. Valid layouts,
   future layouts and unidentified corruption cannot enter this exception.
   DLAY record 12, payload `{1}`, is a bounded recovery provenance marker; other payloads
   remain opaque. Diagnostics persist through save/reopen and ordinary layout edits;
   Workspace > Reset layout acknowledges it. No user workspace file is changed.
   Save recovery and the snapshot share the non-RT state transaction, with no callbacks
   or I/O. A concurrent newer committed layout edit is never overwritten.
2. Pad and step/range Inspector follow/pin work, including explicit A/B pin addresses.
   Song step numeric edits use the resolved bar and retain copy-on-first-real-edit,
   source isolation and exact Undo/Redo. Song bar target/status Inspector, explicit
   Edit Source Pattern navigation and sample context remain unfinished. Kind3 Song
   pins still preserve their address and show unavailable with Unpin; Song step Pin
   is disabled until that context exists, never persisted as a misleading pattern pin.
3. SOUND_SECTIONS still needs collapsible-section presentation. Graphical PARAM_LANES
   now exists and restores from DLAY; multi-step/range numeric editing is implemented.
   Existing step/Sound controls remain canonical.
4. Complete keyboard/focus/scroll and actual Windows VST3 peer/content-scale plus
   OS/host DPI 100/125/150/200% evidence. Raster snapshots are insufficient.
5. The normal feature merge now includes StyleLibrary #153 at `83c616d429991d8b8da01bdd27d11002630b2103`,
   preserving both parents and its failure/Unicode/UUID regressions. This is preparation
   in Draft #163, not integration-ref mutation or independent approval of #153. Native
   library import/export/management UI still needs wiring to this existing service.
6. Exact integrated-head Windows build/full suite/validator/artifact hashes, RT/state/
   undo/Song/MIDI/nine-output gates; factory licensing/listening and one final owner
   Cubase/REAPER pass. No integration/main/release ref changes or auto-merge.

Gate 1 is accepted in #127 comment 6980638479. Do not reopen browser polishing or
another owner visual-approval loop. Arena owns #156 FX and #160 browser work.
