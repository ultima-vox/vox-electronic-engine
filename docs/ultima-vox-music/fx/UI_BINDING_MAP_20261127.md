# VOX FX — native UI binding map (workstation rebuild)

Scope: FxPanel native workspaces (TZ §13.2), commit lineage `651da82` → `26b8356`.
This map enumerates every visible control, its real binding, and its ownership
domain. Controls are grouped per workspace; overlays are listed under their
entry points. Every control binds to either a host parameter (19 frozen IDs in
`products/fx/parameters.csv`), Program/Pattern state (create-only, part of the
state container), a bounded engine action, or a file operation. There are no
duplicate controllers and no invented controls: each control appears in exactly
one workspace (asserted by `suite_fx_ui_workspace_keyboard_tests`), and the
editor writes nothing that is not backed by the processor.

Ownership domains (TL P0 7227421044):

- Host MIDI — `TriggerSource::midi`, channels 1–16, per-channel/note ticket
  FIFOs. CC120/123 on any channel affects only that channel. Global Panic
  (transport/`reset`) reaches all voices.
- Editor keyboard — `TriggerSource::ui`, engine channel-0 domain with an
  independent per-note ticket FIFO. Never written to the host `MidiBuffer`
  (no synthetic MIDI echo). UI notes are dropped while `!admit`/offline;
  offline bounces discard live UI voices; panic/restore/editor-close flush
  the queue and the release latch (fail-closed overflow cannot strand a gate).
- Host indication — `hostNoteMask()` snapshot of actual host sources only;
  display-only on the keyboard (`setExternalNotes`).

## Always-visible chrome

| Control | Binding | Domain |
| --- | --- | --- |
| Workspace tab strip (8) | `FxPanel::setWorkspace` | editor-local view state |
| Peak/clip meter + EV/GR/DROP strip | `peaks_/clipLatched_/audioTelemetry()` snapshots | paint-only telemetry |
| Host clock readout | playhead/time signatures from the host | paint-only |
| PANIC button | `keyboard_.releaseAllNotes()` then `processor_.panic()` | engine global all-off |
| Performance keyboard | `pushUiNote` SPSC queue → `engine_.noteOn/noteOff(0,…,TriggerSource::ui)` | editor-owned voices |
| — pointer | down/drag-across/up, paired release, drag-outside releases | |
| — Space/Enter, Z/X | focus-scoped, captured-key release (poll for key-up) | |
| — OCT-/OCT+, VEL | range shift (releases first), explicit fixed velocity | |
| — external notes | `hostNoteMask()` per-block snapshot | display only |

## PERFORM (default workspace)

| Control | Binding | Domain |
| --- | --- | --- |
| OUTPUT knob | `fx.output.gain` | host parameter |
| DURATION knob | `fx.event.duration` | host parameter |
| MACRO 1…8 knobs | `fx.macro.1…8` (SliderParameterAttachment, live readback) | host parameters |
| Family / Event bars | Program `family`/`bars` | Program state (structural) |
| Play mode / Trigger policy | Program `mode`/`triggerPolicy` | Program state (structural) |
| Factory candidate / A-B compare | factory candidates + compare toggle | Program state |
| Seed / Generate / Mutate | generator actions (structural submit) | Program state |
| AUDITION / STOP | `auditionRequest_` (preview voices) | engine audition |

## DESIGN

| Control | Binding | Domain |
| --- | --- | --- |
| Layer cards A–D | per-layer enabled/solo/family/gain/pan/start/length + curve editor | Program state (structural) |
| Card SOURCE buttons | open SourcePanel preselected on that layer | editor overlay |
| Layer lock row | generator field locks | generator state |
| SOURCE button | SourcePanel overlay (Escape closes, focus returns) | editor overlay |
| Program curves toggle | switches to TIMELINE and back | editor-local |

## TIMELINE

| Control | Binding | Domain |
| --- | --- | --- |
| Scope selector | Program + Layer A…D (5 scopes × 4 trajectory kinds = 20 targets) | Program state |
| Trajectory selector | Amplitude / Pitch / Cutoff / Panorama | Program state |
| Curve editor | nodes/shapes; structural undo/redo/reset | Program state |

## MOTION

| Control | Binding | Domain |
| --- | --- | --- |
| Embedded 64-route editor | `macro::Config` routes, steps, ADSR, clocks (`publishMotion`) | Program state (structural) |
| SOURCE/TARGET/VALUE/DEPTH row | macro routing assign + readback (`fx.macro.*`) | host parameters |

## PROCESS

| Control | Binding | Domain |
| --- | --- | --- |
| FX 1–4 type / wet / amount | `fx.slot.N.wet`/`fx.slot.N.amount` + wet toggle state | host parameters |
| Tail status | file-export telemetry readout | paint-only |

## TRIGGER

| Control | Binding | Domain |
| --- | --- | --- |
| Pattern enable / select | `fx.pattern.enabled`, pattern selection | host parameter + Program state |
| 16 step buttons + per-step note/velocity/probability/ratchet | pattern grid | Program state (structural) |
| Length / bar / grid / page / swing | pattern metrics (swing auto-syncs to tempo) | Program state |
| MIDI export | `startMidiExport` (create-only policy) | file export |

## PRESETS

| Control | Binding | Domain |
| --- | --- | --- |
| Embedded preset browser | library scan/search/facets/favorites/preview/load/save | file system + library index |
| Loose-file handling | `choosePreset` (create-only save policy) | file export |

## ADVANCED

| Control | Binding | Domain |
| --- | --- | --- |
| Import sample / Built-in | sample store acquisition with token guard | engine sample store |
| WAV export | WavOptionsPanel (rate/format/note/velocity) → `renderWav` | file export (create-only) |
| Prepare MIDI drag / drag grip | `prepareMidiDrag` → native OS drag of a closed file | file export |

## Editor overlays (unchanged behaviour)

SourcePanel, GeneratorPanel, WavOptionsPanel keep their open/Escape/focus-return
behaviour on top of the current workspace. Motion and Presets are embedded
workspace panels (no launch buttons). The browser's close returns to PERFORM.

## Tests

`suite_fx_ui_workspace_keyboard_tests`: workspace visibility matrix, nonoverlap
geometry at 1040 px for all 8 workspaces, tab/toggle contracts, keyboard
pointer/keyboard ownership (captured-key releases), drag outside/re-entry,
one-shot event flash, a11y labels, 20 timeline bindings, contextual source
entry, embedded motion/presets, and the processor ownership/queue suite
(same-pitch ch16 UI/host, CC123 scoping, panic/restore, offline transitions,
saturation with latched releases, no MIDI echo, indication-mask correctness).
