# Rack parameter and state contract

Implementation candidate: PR #62, task #56. This is the shared static C++ provider
adapter; it does not implement Acid/Bass controls or change the external C ABI.
Instrument-specific parameter meanings belong to each provider descriptor.

## Named instrument endpoints

A provider opts in with `InstrumentDescriptor::directParameters = true`.
Each base value has a stable `ParameterDescriptor.id`, name, unit, group, type,
minimum, maximum, default and step; choices have ordered stable labels.

**Fixed host pool** (owner decision 27.10.2026, Kontakt model): every slot
exposes the same 64 generic endpoints `slotNN.pKKK` (NN=01..16, KKK=001..064),
created once at processor construction whatever the catalog contains. Loading
an instrument binds each named parameter to the slot's pool endpoint given by
its required, unique `ParameterDescriptor::poolIndex` (0..63), independent of
descriptor order; a bound endpoint takes that parameter's range, name ("Slot N Name"),
unit and value text and the host is told that parameter info changed. Unused
and dormant endpoints are unbound ("Slot N Param KK", neutral value 0).
Automation belongs to the slot: after replacing the instrument, the same lane
drives the new instrument's parameter at that index. A bound endpoint also takes
the parameter's host type: boolean (2 steps), choice (label count) and integer
(inclusive span) are discrete; floating and unbound endpoints are continuous.

Pool-index stability: a parameter's `poolIndex` never changes within one
InstrumentId and is never reused for another parameter; reordering or inserting
descriptor entries does not move any endpoint (regression-tested). Registration
rejects a missing, out-of-range or duplicate pool index. Packages carry the
same explicit index in `params.json` (instrument packages, phase 2). The former per-instrument
IDs `slotNN.sound.<InstrumentId>.<ParameterId>` are removed without migration.

Registration rejects incomplete/duplicate descriptors, runtime key collisions
and VST3 parameter-ID hash collisions. The pinned JUCE VST3 hash policy is
checked against the complete parameter layout.
All direct endpoints are automatable; `preferredMacro` must be -1. Boolean domains
are 0/1; integer and choice bounds are exact float integers within ±16,777,215;
choices contain at most 256 labels. Floating values use the declared range and
step, or continuous values when step=0. Defaults must be finite and in range.

Bounds: 64 direct parameters per instrument (the pool size), 512 unique named
parameters in the compiled catalog. Only the bound bank's at most 64 cached
atomics are read per slot/block.
Catalog registration and APVTS construction occur outside processing. Catalog
changes that alter existing endpoint meaning require explicit review/versioning.
The human generation seed is the exact integer domain 0..16,777,215; deterministic
per-instrument/slot seed hashing retains its separate 32-bit domain.

## Source of truth and ownership

APVTS owns live plain base values and DAW automation. One ordinary endpoint changes
one base parameter. The controller stores named Rack values for durable identity
and unresolved modules; a project capture samples live host bases into those
named values. On restore the validated named Rack values are authoritative for
active instrument banks. A Sound preset updates only its selected slot's complete
named bases; it does not replay another slot's live edits or replace Pattern.

A direct Sound preset supplies the complete named set, without macro values.
Optional versioned native state is separate: absent preserves native state,
present-empty requests native Init, present-data is checked against schema and
provider/host budgets. Prepare native state first, then apply named bases.
Modulation derives effective values from current bases and never writes APVTS.
Nonfinite live direct values are ignored in favour of the committed base.

The controller serializes mutations and non-RT snapshots. A bounded audio gate
never waits or locks: overlapping mutation blocks render silence. The next admitted
block clears voice/MIDI ownership, generated schedulers, keyboard/audition state
and ducking to recover a potentially skipped NoteOff. Module construction and
metadata allocation are off audio; Rack publication moves prepared state without
throwing. Such controller transactions may interrupt sustained notes; they are
not advertised as sample-continuous hot-swaps. Existing host suspend state is
not overridden by internal transactions.

Keyboard audition crosses a fixed 2048-event SPSC queue. JUCE keyboard listeners
serialize producers on the controller side; audio only consumes raw fixed-size
MIDI events and never enters the keyboard lock. Long SysEx is rejected from raw
MidiBuffer metadata before constructing a MidiMessage. Host scanning is bounded
to 2048 events per block; dense-host or audition-queue overflow clears ownership
through deterministic Panic, rather than dropping a final NoteOff indefinitely.
Deferred GUI cleanup uses controller-side `allNotesOff`, which emits queue events;
a silent GUI reset must not erase ownership of a fresh post-Panic key.

Bulk fixed processing state (Router ownership tables, routed MIDI buffers,
slot runtime bindings and default block controls) is allocated once in Rack
construction. Reset/process never allocate or replace that storage. Compile-time
limits require the inline Rack object <=64 KiB and the processor object <=256 KiB;
large objects must not consume a host's ordinary 1 MiB stack before nested calls.
This changes placement only: capacities, IDs, payloads and state versions are unchanged.

The shared native editor owns local SequenceEdit snapshots. Controller commits
check slot/revision, rejecting stale edits after a restore or generation. Timer
refresh follows changed revisions/module identity without republishing stale UI
models. Borrowed test/controller-only model access requires external serialization.

## Project, scene and failure policy

Engine project schema **5**, Rack schema **4**, FX schema **2**. Obsolete POC
project/complete-scene formats are explicitly rejected; no parallel compatibility
mode is introduced. Plugin bundle/FUID/manufacturer remain unchanged. Complete
scenes use `format="rack-project-v5"` and the same whole-project capture/restore
path as host state, rather than separate partial APVTS/sequence commits.

Before parsing, host chunks and scene files are capped at 32 MiB. Validation
requires unique PARAM IDs, finite numeric scalars, every endpoint of the fixed
layout (pool included) and in-range values for non-pool endpoints. Pool PARAMs
only mirror the Rack's named values and need only be finite; the named values
are validated strictly against the bound descriptor and win on restore. Rack
requires its complete unique named parameter set.
Patterns, FX and module candidates are prepared before publication; invalid
values, construction exceptions, null factories and prepare failures reject the
transaction. Missing/incompatible modules instead retain InstrumentId, versions
and opaque payload in an explicitly unresolved silent slot, without substitution.

An incompatible module keeps its parameter domain: its finite named values and
native payload remain authoritative in the unresolved Rack slot, even outside
today's installed descriptor range. Its pool is unbound and neutral, so no
out-of-range value reaches today's DSP/UI. No secondary opaque cache or
automation rewrite is introduced. A compatible newer provider rebinds the pool
and recovers the original values. Explicit module replacement discards that saved domain;
active compatible values and all non-finite values retain strict rejection/rollback.

Rack preparation failure retains the previous committed APVTS, patterns and Rack.
DSP histories, envelopes, meters and tails are runtime state; successful restore
resets them. Current parameter values and explicit native payload are persistent.
For signal topology, tap/reset/duck policy and numeric defaults see
[Rack audio routing](RACK_AUDIO_ROUTING.md).

## Rack pattern MIDI export

`serializeSlotPatternMidi` snapshots any populated immutable slot bank 0..15 on
the controller thread and emits a Standard MIDI File. It shares the canonical
Sequence export builder with the scheduler/export timing contract. The selected
helper uses the currently selected slot; the explicit slot helper never aliases
an invalid index to slot01. Probability uses the same stored global seed,
InstrumentId and SlotId domain as generated playback.

Export represents the authored pattern at the Rack input: transpose, zones,
slot mute/solo/power, fader, FX and ducking are not baked into MIDI. Reimporting to
the same Rack therefore does not apply them twice. The assigned input channel is
written; MIDI Off uses neutral channel1 for exporting to another instrument and
requires explicit host routing for reimport. Empty/unpopulated/all-skipped patterns
fail with a diagnostic while preserving the caller's previous MemoryBlock.

Settings exposes Export selected pattern MIDI. Encoded bytes are captured before
the asynchronous native file dialog; later selection, generation or project
restore cannot change that pending export. It writes the exact chosen .mid/.midi
path, using checked temporary write/flush before atomic target replacement.
The temporary-file helper creates unique caller-owned paths across slots and
instances; a drag/drop consumer must remove its own file after use. No file I/O
or serialization runs in the audio callback. Actual host import/drag/native-dialog
acceptance remains a milestone check, separate from SMF parsing tests.

## Latency and verification boundary

The production processor admits only descriptors declaring zero latency. The
current static modules and returns are zero-latency. Nonzero-latency providers
require a reviewed parallel-path compensation design before admission; there is
no implicit promise that a max-latency getter alone supplies PDC.

Independent contract, APVTS, actual-processor state/scene, mutation-gate, routing,
FX-tail and native editor tests accompany this adapter. These prove infrastructure
behaviour under their fixtures, not production timbre or Cubase/REAPER acceptance.
