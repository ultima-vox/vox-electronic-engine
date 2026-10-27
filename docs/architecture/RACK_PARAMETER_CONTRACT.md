# Rack parameter and state contract

Implementation candidate: PR #62, task #56. This is the shared static C++ provider
adapter; it does not implement Acid/Bass controls or change the external C ABI.
Instrument-specific parameter meanings belong to each provider descriptor.

## Named instrument endpoints

A provider opts in with `InstrumentDescriptor::directParameters = true`.
Each base value has a stable `ParameterDescriptor.id`, name, unit, group, type,
minimum, maximum, default and step; choices have ordered stable labels. Host IDs
are `slotNN.sound.<InstrumentId>.<ParameterId>` (NN=01..16). Slot banks remain
immutable; display selection never changes their automation ownership. Existing
plugin/parameter/InstrumentId identities are retained.

Registration rejects incomplete/duplicate descriptors, ambiguous concatenated
host IDs, runtime key collisions and VST3 parameter-ID hash collisions. The
pinned JUCE VST3 hash policy is checked against the complete parameter layout.
All direct endpoints are automatable; `preferredMacro` must be -1. Boolean domains
are 0/1; integer and choice bounds are exact float integers within ±16,777,215;
choices contain at most 256 labels. Floating values use the declared range and
step, or continuous values when step=0. Defaults must be finite and in range.

Bounds: 128 direct parameters per instrument, 512 unique direct endpoints in the
compiled catalog, replicated over at most 16 immutable host banks. Only the
selected instrument bank's at most 128 cached atomics are read per slot/block.
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
requires unique PARAM IDs, finite numeric scalars, installed endpoint ranges and
discrete domains, and every fixed shell/routing endpoint. New `.sound.` banks
may be absent in compatible catalog snapshots; unknown module endpoints remain
opaque finite scalars. Rack requires its complete unique named parameter set.
Patterns, FX and module candidates are prepared before publication; invalid
values, construction exceptions, null factories and prepare failures reject the
transaction. Missing/incompatible modules instead retain InstrumentId, versions
and opaque payload in an explicitly unresolved silent slot, without substitution.

Rack preparation failure retains the previous committed APVTS, patterns and Rack.
DSP histories, envelopes, meters and tails are runtime state; successful restore
resets them. Current parameter values and explicit native payload are persistent.
For signal topology, tap/reset/duck policy and numeric defaults see
[Rack audio routing](RACK_AUDIO_ROUTING.md).

## Latency and verification boundary

The production processor admits only descriptors declaring zero latency. The
current static modules and returns are zero-latency. Nonzero-latency providers
require a reviewed parallel-path compensation design before admission; there is
no implicit promise that a max-latency getter alone supplies PDC.

Independent contract, APVTS, actual-processor state/scene, mutation-gate, routing,
FX-tail and native editor tests accompany this adapter. These prove infrastructure
behaviour under their fixtures, not production timbre or Cubase/REAPER acceptance.
