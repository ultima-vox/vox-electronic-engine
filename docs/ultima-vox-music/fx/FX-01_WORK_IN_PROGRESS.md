# FX-01: interim instrument foundation (not a release)

This is a chronological development log: early sections describe historical
checkpoints and are superseded by later sections. For current milestone status
and remaining work, see `STATUS.md`. The standalone identity, stereo-only
output and MIDI input remain unchanged; the product is not complete.

## Initial FX-01 checkpoint behavior (historical)

* `fx.output.gain` is a normalized linear host parameter, default 1.0,
  smoothed over 5 ms. Its stable ID and range are in `products/fx/parameters.csv`.
* FPRM is an FX product section within VOXS schema 1. It contains a little
  endian 32-bit version (1), then an IEEE-754 32-bit normalized gain. Missing
  FPRM imports FOUND-01 shell states at the default; malformed, future-version
  or non-finite/out-of-range values are rejected before any state commits.
* NoteOn is sample-accurate and starts a one-shot; NoteOff and velocity-zero
  NoteOn do not cut it off in this interim mode. CC120 and CC123 fade events
  on the addressed MIDI channel over 5 ms. Host reset fades all channels.
  This policy is not the final configurable trigger-mode contract.
* Up to eight active events and one 5 ms retiring event are preallocated. A
  ninth event retires the oldest; a subsequent trigger while retirement is
  occupied is dropped. This is a bounded interim policy, not configurable
  stealing. The four-second one-shot has a 20 ms end fade and no effects tail.
* The prepared coefficient table linearly interpolates 4097 points over the
  event. Its source/reference is the preceding checkpoint's per-sample
  exponential implementation, preserved as a test-only reference. Render
  overwrites both output buffers and requires valid pointers for the span.

## Local observations, not qualification

On Linux, Intel Xeon 2.60 GHz, GCC 12.2.0, `-O2`, 300 consecutive tests of
128-sample DSP renders, preparation and trigger excluded. These are wall-clock
microbenchmarks, not plugin callback timing, CPU-load measurements or Windows
results. Nine voices means eight active plus one retiring (first 128 samples).

| Sample rate | Voices | p50 us | p95 us | p99 us | max us |
|---:|---:|---:|---:|---:|---:|
| 44100 | 1 | 0.78 | 1.41 | 1.79 | 2.52 |
| 44100 | 8 | 3.95 | 6.07 | 14.39 | 45.32 |
| 44100 | 9 | 4.42 | 7.36 | 12.79 | 26.57 |
| 48000 | 1 | 0.83 | 1.38 | 1.75 | 4.62 |
| 48000 | 8 | 3.83 | 6.24 | 7.61 | 24.87 |
| 48000 | 9 | 4.27 | 4.42 | 11.91 | 27.26 |
| 96000 | 1 | 0.74 | 0.92 | 1.01 | 1.15 |
| 96000 | 8 | 3.71 | 3.74 | 7.96 | 19.96 |
| 96000 | 9 | 4.16 | 4.19 | 9.70 | 19.26 |

Across each full four-second single-event reference render at the three rates,
maximum absolute sample error was 3.73e-9 and maximum relative RMS error
5.16e-8. The test thresholds are 1e-6 and 1e-5 respectively. An additional
first-difference energy test checks that later energy rises; it is not a
complete spectral/aliasing quality assessment. The DSP test was also run
with AddressSanitizer and UndefinedBehaviorSanitizer. Windows performance,
actual plugin callback distributions, host validation and allocation/locking
proof await exact-head CI and further independent analysis.

## Subsequent development

An interim Perform panel binds the real output parameter using JUCE's slider
parameter attachment and provides coalesced audition/panic requests and a
meter sourced from the rendered block peak. It is not the full product UI.
The JUCE-free timeline primitives now provide validated 2–16 node curves,
seconds and tempo-integrated bars clocks, and per-layer time mapping. Only
the seconds clock is currently used by the Noise Riser; bars, layer mapping
and curve editing are not yet connected to processor state or the UI. The
DSP API can snapshot 0.05–32 second durations per trigger; the processor still
uses its fixed four-second default. No claim of complete FX-02 is made.

## Layered noise engine checkpoint

The JUCE-free Noise Riser engine can snapshot four independently enabled
noise layers per event. Each layer has its own normalized event offset/span,
gain, pan, gain curve and pan curve; enabled layers are averaged per event to
keep the existing output bound. Eight event slots plus one retirement slot
remain fixed-size. The UI/host cannot yet edit, publish or serialize these
layer settings; the plugin uses the original single-layer default. Pitch,
cutoff curves, other sound families and a realtime-safe Program publication
bridge are still absent. `setProgram` is only safe with exclusive engine
access and must not be called directly by a concurrent UI thread.

After adding layers, a repeat of the Linux DSP-only 128-sample benchmark
(same compiler/host and method as above; one active layer per event) produced:

| Sample rate | Voices | p50 us | p95 us | p99 us | max us |
|---:|---:|---:|---:|---:|
| 44100 | 1 | 4.51 | 4.70 | 6.49 | 35.94 |
| 44100 | 8 | 16.64 | 25.56 | 30.94 | 40.35 |
| 44100 | 9 | 18.71 | 28.47 | 51.45 | 62.73 |
| 48000 | 1 | 4.66 | 5.18 | 5.33 | 22.27 |
| 48000 | 8 | 23.18 | 29.38 | 60.44 | 115.32 |
| 48000 | 9 | 21.61 | 30.23 | 46.97 | 51.73 |
| 96000 | 1 | 4.54 | 5.99 | 23.78 | 47.09 |
| 96000 | 8 | 26.64 | 32.90 | 66.72 | 70.97 |
| 96000 | 9 | 19.27 | 29.24 | 44.97 | 53.03 |

These results supersede the earlier pre-layer benchmark, but are not a
four-layer stress benchmark, a real host callback distribution, or Windows
performance evidence. Optimisation and worst-case measurement remain required.

## Duration parameter and state v2

`fx.event.duration` is a host-automatable 0.05–32 s value. The processor
snapshots its value once per block for newly triggered events; changing it does
not alter events already playing. The Perform panel binds it via a JUCE slider
parameter attachment. The reported tail is 32 seconds. FPRM v2 appends the
normalized duration after the normalized output gain; v1 and the original
shell's absent FPRM import with the default 4 seconds. State restores still
use the SuiteProcessor transactional product-section protocol. FX-02's
bar-based timing and transport behavior are not connected yet.

## Program bridge foundation

A fixed three-slot, single-producer/single-consumer Program bridge validates
before publication, reports a full queue, and copies only the newest claimed
snapshot into caller-provided storage on the consumer. Late observations of
older snapshots are discarded. It uses lock-free 32-bit atomic slot ownership;
there is no audio-thread allocation or object graph retirement. Local stress
and ThreadSanitizer tests exercise snapshot consistency. The bridge is not
wired to the plugin or host state yet: doing so requires a controller that
serializes all edits and coordinates transactional restore without losing
publication when the bounded queue is full. It must not be called concurrently
by multiple producers.

## Transport discontinuity policy (interim)

When a host supplies playing state and absolute sample positions, a transition
from playing to stopped or a sample-position discontinuity while playing
fades currently active events over 5 ms at the next block boundary. A note or
editor audition in that block can still start a new event. Missing playhead
information clears continuity history instead of assuming a stop/seek. Tests
using a simulated JUCE playhead were added but require the processor test to
run in the suite build; Cubase/REAPER behavior has not been verified.

## Downlifter engine foundation

A second layer recipe combines a descending MIDI-transposed sine sweep and a
falling noise low-pass/envelope. Frequency and filter trajectories are prepared
outside the callback. Local renders at the three target sample rates and block
sizes pass finiteness, audibility, broad brightness trajectory and short-window
fundamental-energy checks, including ASan/UBSan. This is not yet available in
plugin Program state or the UI; audible review, alias/CPU qualification and
full FX-03 acceptance remain outstanding.

## Interim family selector

The Perform panel now selects Noise Riser or Downlifter as a structural choice,
not a host-automatable parameter. An atomic integer is read at block boundaries;
the audio thread creates the bounded layer settings and existing events keep
the family they started with. The `FFAM` product section stores version 1 and
the validated family ID as two little-endian u32 values. States missing FFAM
migrate to Noise Riser; unsupported versions and IDs are rejected. This is a
single-scalar handoff, not the full layered Program controller. Processor-level
family, state and editor tests have been added, but have not yet run in a full
JUCE build or VST3 host.

## Shepard sweep

A six-partial octave-overlap tonal sweep is selectable as FFAM family 2.
Per-partial Hann-like amplitude windows suppress octave-wrap discontinuities;
partials fade before Nyquist. Local DSP tests render complete two-second
sweeps at 44.1/48/96 kHz, compare 64/512-sample blocks, check finite bounded
sound, and compare against the noise riser. ASan/UBSan passed locally. This
is an initial implementation: perceptual octave continuity, alias rejection,
worst-case CPU and processor/host behavior still require qualification.

## Impact / Boom

FFAM family 3 adds a descending sine body, separate sub and short noise
exciter with prepared exponential trajectories and a 1 ms attack fade. The
local DSP test covers complete four-second events at 44.1/48/96 kHz and
64/128/512 blocks; audibility, finite output and decaying tail pass, as does
ASan/UBSan. This does not establish perceptual quality, aliasing, maximum-load
performance or plugin-host validation.

## Whoosh / Doppler

FFAM family 4 adds a filtered noise/tonal pass with a fixed 256-sample per-layer
own-signal delay, interpolated moving read tap, an amplitude window and stereo
pan motion. Delay is bounded to 254 samples (at most 2 ms where the sample
rate permits). Tests at 44.1/48/96 kHz and 64/128/512 blocks check audibility,
finite output and left-to-right energy movement; ASan/UBSan passed locally.
The delay increases per-event memory and note-on initialization; real host
callback and maximum-load performance remain unmeasured. Listening and full
Doppler/alias qualification remain outstanding.

## Zap / Laser

FFAM family 5 adds an exponential MIDI-transposed sine chirp with a short
noise tick and rate-independent precomputed pitch/amplitude trajectories.
Tests cover 44.1/48/96 kHz, 64/128/512 blocks, output bounds, decay and a
normalized high-frequency trajectory proxy; ASan/UBSan passed locally. The
current recipe does not include optional FM, drive or oversampling; listening,
anti-alias measurement and exact-head host tests remain required.

## Psy Blip / Squelch

FFAM family 6 uses a pitch-falling sine/noise exciter and a moving resonant TPT
low-pass with precomputed coefficients. Local 44.1/48/96 kHz and 64/128/512
sample tests cover finite output, decay and distinction from Laser. An
additional eight-event/four-layer stress render at 96 kHz stayed finite and
below full scale; ASan/UBSan passed locally. Comb, ratchet, perceptual
acceptance, full filter inventory and exact-head VST3/host evidence remain.

## Metallic / Resonant

FFAM family 7 implements eight prepared, damped inharmonic modes with bounded
feedback coefficients and omission above 0.45 times sample rate. Local tests
cover full events at 44.1/48/96 kHz and 64/128/512 blocks, decay and finite
output, plus eight-event/four-layer stress at 96 kHz; ASan/UBSan passed.
This is an initial mode bank, not the full filter/controls inventory or a
perceptual/alias/performance qualification.

## Texture / Drone

FFAM family 8 uses two continuously phased detuned sine oscillators, filtered
noise and slow level/pan motion. Local 44.1/48/96 kHz, 64/128/512 block
renders demonstrate sustained output, stereo movement and finite bounds;
ASan/UBSan passed. No unison control, full VA waveforms or product-level
quality/host/performance qualification is claimed.

## Formant / Alien

FFAM family 9 uses three moving, prepared-coefficient TPT band-pass formants
excited by sine and noise. Local tests cover 44.1/48/96 kHz, 64/128/512
blocks, finite output, distinction from Texture and 96 kHz eight-event,
four-layer stress; ASan/UBSan passed. Full vowel control, modulation routing,
perceptual/alias qualification and host verification remain outstanding.

## Layered Program publication and persistence

The processor now persists the current four-layer Program in a versioned
`FPRG` section (explicit little-endian fields and bounded curves). Legacy
states without FPRG migrate from FFAM; mismatched FFAM/FPRG or malformed,
future and invalid Program data are rejected before commit. Audio reads use a
single bounded attempt against fixed lock-free atomic words: a concurrent
writer causes the old snapshot to remain active until a later block. Writers
serialize on non-audio threads; no resource graph is retired in the callback.
The old three-slot bridge remains a standalone experimental component and is
not used by the processor. Controller APIs expose Program updates, but a full
layer/curve editing UI is not yet implemented.

Standalone codec tests include truncation, random inputs, unsupported version
and transactional failure. Program-store concurrent tests passed under TSan;
Linux allocation probe measured zero allocations for a changed snapshot read.
JUCE processor integration tests for four-layer audio and project restore were
added and syntax-checked only; they await a runnable suite build and host tests.

## Basic layer controls

The interim panel exposes enable, family, gain, pan, start and length for each
of the four layers. These are structural Program edits for newly triggered
events, not host automation; layer gain/pan automation and editable curves
remain required. Layer edits use serialized non-audio read-modify-write so
concurrent changes to different layers are not lost. JUCE editor binding tests
were added and syntax-checked but have not yet executed in a full suite build.

## Basic gain-curve editing

Each layer card has an editable gain curve: double-click inserts a node,
interior nodes drag with 1/16 time snapping, Delete removes an interior node,
and L/S/E/H set the outgoing shape. Ctrl-Z/Y undo and redo structural edits;
project restore clears the editor undo history. Curve values publish through
the Program store and are captured by newly triggered events. Tests for the
editor-to-Program binding and undo/redo were added but only syntax-checked.
The full Timeline workspace, selection/zoom, pan/pitch/filter/global curves,
accessible alternative node entry and Windows visual/DPI qualification remain.

## Modulation primitives

A JUCE-independent, bounded 64-route typed matrix, LFO and seeded
sample-and-hold have been added. The matrix applies signed offsets to a copy
of normalized host base values and prepares route smoothing off the callback.
Standalone tests cover validation, clamping, smoothing, reproducibility and
zero allocations in a Linux audio-path probe; ASan/UBSan passed. These helpers
are not wired into Program state, processor DSP or UI. Other sources, macro
assignments, route editing and complete FX-04 behavior remain outstanding.

## Windows stack safety in tests

The prepared engine currently occupies approximately 549 KB (local Linux
`sizeof`, including coefficient tables). Tests that create multiple engines or
processors now allocate them on the heap rather than relying on the default
Windows thread stack. This is an unverified Windows safeguard; exact-head
Windows execution remains pending. Engine memory and initialization costs
still need product-level profiling and optimization.

## Glitch / Stutter

FFAM family 10 captures the layer's own synthesized signal in its bounded
256-sample buffer, then loops windowed slices with deterministic gated
retriggers. The interim grid is fixed at 120 BPM; it is not host-synchronized
and has no sample-import/slice editor yet. Local tests at 44.1/48/96 kHz and
64/128/512 blocks check audibility, deterministic gate silence and finite
bounds; ASan/UBSan passed. Listening, artifact, transport-sync and VST3 host
qualification remain outstanding.

## Granular Cloud

FFAM family 11 uses an explicitly built-in, procedurally synthesized source
prepared outside the audio callback. A single preallocated pool contains at
most 64 Hann-windowed grains globally; note stealing remaps ownership and
panic retires grains. Local tests at 44.1/48/96 kHz verify audibility,
block-invariant deterministic output, the 64-grain cap and retirement. A
Linux allocation probe observed zero allocations during granular processing
and event stealing; ASan/UBSan passed. This is not full granular acceptance:
user sample import, safe sample ownership/retirement, reverse/position/rate
controls, sample portability, tuning and host/quality/performance evidence are
still required. The built-in source is not a hidden fallback for a missing
user sample; user sample loading is not yet offered.

## Generator foundation

A JUCE-independent seeded generator produces layered Programs with category,
style, complexity, intensity, energy, stereo movement and seconds; it supports
per-layer/per-field locks, mutation and distinct A/B seeds. A deterministic
48-entry procedural seed corpus covers four variants per currently implemented
family. Local tests verify determinism, lock preservation, structural uniqueness
and an audible finite smoke render of each entry; ASan/UBSan passed. These are
not yet curated/approved factory presets. Generate and Mutate are now wired to
the plugin, but the corpus, granular locks and generator settings are not yet exposed
or fully saved; key/pitch, patterns, full musical controls, user-facing exports
and perceptual/licensing qualification remain outstanding.

## Trigger-pattern and MIDI-export foundation

A JUCE-free offline scheduler supports one to four 4/4 bars of 1/16 steps,
seeded probability, one to four ratchets, trigger notes and velocities. It
exports a type-0 Standard MIDI File from the same canonical tick events; tests
parse the file and compare notes, note-offs and offsets, check bounds,
determinism and 12 structurally distinct seed patterns. ASan/UBSan passed.
This is not yet the internal realtime sequencer or a product export feature:
host synchronization, other grids/triplets, swing, UI, state, file delivery and
musical validation remain. MIDI represents triggers only, not synthesized audio.

## Offline WAV foundation

A worker-only renderer creates stereo 24-bit PCM or 32-bit float RIFF/WAVE
bytes using the same event DSP and initial effects, bounded duration/rate, cancellation and atomic
progress. It returns bytes transactionally and rejects non-finite/out-of-range
samples instead of silently clipping. Tests validate headers, sample/frame
counts, exact float samples, decoded PCM error and cancellation; ASan/UBSan
passed. No file chooser, background job integration, atomic file replacement,
patterns or configurable tails are connected yet. A lowpass slot appends a
fixed one-second tail; full future effect-tail management remains unimplemented.

## Generator controller integration

The interim Perform panel now publishes deterministic Generate and Mutate
results through the serialized ProgramStore controller. Existing events keep
their captured program; new events see the published validated snapshot.
Generate/Mutate deliberately leave host-automated duration and output gain
untouched. Per-field lock support is available in the controller API, but the
UI exposes whole-layer lock toggles plus family and seed (four generated
layers, default settings); subsequent FGEN work saves the seed and masks. Program content is saved in existing FPRG state;
the seed and generator settings themselves are not saved yet. Full generation,
A/B, lock controls, preset browser and recallable metadata remain unfinished.
Local ProgramStore and generator tests pass, and processor/editor sources and
tests pass syntax checks; the new UI/state integration test is not executed.

## Shared effects foundation

A standalone four-slot serial, allocation-free zero-tail chain implements
lowpass, drive, wavefold, bitcrush, ring and gate, true bypass and smoothed
wet/amount controls with 5 ms crossfades on structural changes. Local tests
cover finite bounded output, each type changing audio, 4 slots, exact steady
bypass, block invariance, validation, and a Linux callback allocation probe;
ASan/UBSan passed. This foundation is now wired to processor audio, basic Perform controls,
versioned FXSL state and the worker renderer. Eight stable host-automatable
wet/amount parameters feed the DSP without altering structural slot types;
FXSL v2 saves full float values, and v1 quantized values migrate. Generate and
Mutate preserve these host automation lanes. Lowpass state
is flushed at a tiny threshold and the host reports a conservative 33 second
tail; worker WAV appends 1 second when lowpass is enabled. It omits
delay/reverb, phaser/flanger/chorus, full tail/latency policy and
aliasing/performance qualification. It is not a completed FX-04 processor.
Processor/UI integration and state tests have only been syntax-checked, not
executed. Program and four FX settings are now published together via one
bounded lock-free audio snapshot; controller layer/FX edits serialize, and
host state saves the coherent pair. Local concurrent publication stress, TSan
and a Linux allocation probe pass. Host-automated parameter values remain
separate from structural snapshots, as documented above.

## Effect automation precedence

The eight `fx.slot.N.wet` / `fx.slot.N.amount` parameters are host-automatable
and independently smoothed; slot type is structural. Generate and Mutate
change only the layer Program and leave all automation lanes unchanged. Host
state restore applies saved FXSL base values; subsequent host automation wins.
FXSL v1 migrates to v2 and absent sections default to bypass. Callback reads
the coherent structural snapshot plus independent host parameters at block
boundaries. Processor/editor/host automation, v1 migration and malformed-v2
tests are authored and syntax-checked only; executable Windows evidence is
still required.

## Controller-to-WAV render

A non-realtime processor API now captures one coherent Program/slot-type
snapshot, samples the current host duration, master gain and effect base
parameters, and invokes the worker WAV renderer. The caller chooses note,
velocity, sample rate and PCM/float format; cancellation/progress propagate.
A processor-versus-WAV test is authored and syntax-checked, not yet executed.
The API does not create threads or files, and no UI/drag delivery exists yet.
Host automation is sampled, not recorded as a changing automation envelope;
full effects tails and tempo/pattern export still remain.

## Pattern state and controller

FPAT initially used v1 to store bars, seed and 64 step fields in explicit little-endian
words with reserved-bit, length and range validation. Absent FPAT migrates to
an empty default pattern; malformed/future data rejects the whole project
transaction. Program, FX slot types and pattern now share one bounded atomic
publication; the controller exports MIDI bytes through the same canonical
offline scheduler. Local pattern codec, scheduler and concurrent store tests
passed, including TSan, selected ASan/UBSan and a Linux allocation probe.
Processor state/export tests are authored but only syntax-checked. There is
At this checkpoint there was no realtime playback; see the following section
for the current limited host-synchronized implementation.

## Host-PPQ pattern playback (partial)

FPEN v1 stores the independently host-automatable `fx.pattern.enabled`
parameter, default off; missing FPEN migrates to off. The Perform panel can
load one of twelve uncurated seed patterns and enable playback. A fixed 512-event
canonical schedule is published with Program and slot types; while transport
is playing and host PPQ/BPM are valid, 4/4 patterns issue one-shot note-ons at
rounded sample offsets. At equal offsets, incoming MIDI is processed first.
MIDI export and playback share the same ticks and probability choices; output
MIDI remains disabled. Unsupported explicit time signatures and missing/invalid
tempo or PPQ produce no pattern triggers; no tempo is fabricated. The host
tempo is sampled once per block; same-seed probability repeats each cycle.

The standalone scheduler passed 44.1/48/96 kHz, 64/128/512 block, 75/120/173
BPM, cross-boundary rounding, tempo-change and MIDI-tick parity tests; a Linux
allocation probe and ASan/UBSan passed. Processor tests for note timing, state,
and allocation are authored but syntax-only. Missing: runtime Windows/DAW
verification, triplet/other grid and swing options, event editing, per-cycle
variation, transport discontinuity qualification and integrated MIDI file UI.

## Partial pattern grid editor

The Perform panel has a length selector, bar page, sixteen on/off step
buttons and selected-step note/velocity/probability/ratchet controls; edits serialize through the ProgramStore and reach the host scheduler
and FPAT state. Out-of-range bar pages are disabled, not silently extended.
The FX editor minimum height is 760 pixels so existing layer curve controls
retain space; older smaller saved sizes are enlarged on opening. This is not
the final pattern workspace: grid/swing options, accessibility audit, scaling/visual acceptance, and undo are still missing.
ProgramStore tests, TSan and allocation probe passed; UI tests are syntax-only.

## Pattern grid expansion and FPAT v2

Straight quarter/eighth/sixteenth/thirty-second grids and their triplets use
a fixed 960-PPQ timeline; up to 192 steps and 1536 note events cover four
bars with four ratchets per step. The UI adds grid and 16-step page selectors;
changing grid reinterprets step timing and leaves step indices intact. FPAT v2
saves grid and all 192 steps; v1 migrates to a sixteenth grid, filling newly
available steps with defaults. Tests cover all eight grids, MIDI/playback
offset parity, 44.1/48/96 kHz and 64/128/512 blocks, maximum capacity,
malformed/future state and v1 migration. Local ASan/UBSan, TSan store tests
and allocation probe passed. Swing, real DAW transport/tempo transitions,
UI visual/accessibility testing and processor-state runtime tests remain open.

## Pattern swing and FPAT v3

A structural 0–1 swing control delays each odd step by up to half its grid
interval; ratchets are distributed within the shortened remainder of that
step. The same integer tick schedule drives host playback and MIDI export.
FPAT v3 persists swing; v1 and v2 migrate with swing zero. Validation rejects
non-finite/out-of-range swing and unsupported future versions. Local tests
cover swung offsets/order, v1/v2 migration, eight grids at 44.1/48/96 kHz,
64/128/512 blocks and MIDI/playback timing parity, plus ASan/UBSan, TSan and
a maximum-schedule allocation probe. Processor/UI tests remain syntax-only;
DAW transport, varying BPM and real visual/accessibility acceptance remain.

## Macro routing foundation (not yet connected)

An initial typed route config accepts up to 64 macro-to-master-gain routes and
validates source/target/depth/smoothing; an explicit versioned codec and the
unified ProgramStore publication include the routes. Runtime applies bipolar
macro offsets without overwriting the host base. Local routing tests,
ASan/UBSan, ProgramStore TSan and Linux allocation probes passed. No macro
parameters, processor DSP, state section, export or UI is connected yet; other
modulation sources and destinations are not accepted by this initial config.

## Macro integration (partial)

Eight stable `fx.macro.1`–`fx.macro.8` host parameters feed bipolar macro
sources (0.5 is neutral). The current typed routing supports up to 64 routes
from those sources to master gain, each with bipolar depth and smoothing;
FMOD v1 stores routes, FMAC v1 stores base values, and absent sections migrate
to neutral/no routes. The compact Perform selector edits one macro at a time
and assigns a macro-to-gain depth without changing the host gain lane.
Program, pattern, FX structure and route config share one bounded publication;
callback evaluation allocates nothing, and worker WAV uses the same routing.
Generate/Mutate preserve macros and routes. The macro UI exposes one route per
macro, while the codec/runtime can represent 64; other modulation sources,
destinations, general matrix UI, click-free route switching and automation
of route depths are **not implemented**. Processor/UI and state tests are
syntax-only. Local macro/codec/WAV tests, store TSan, ASan/UBSan and allocation
probes passed; target-host RT and audible quality still require qualification.

## Generator control recall

FGEN v1 persists the exposed deterministic seed and four per-layer lock masks,
with strict range/unknown-bit validation; missing FGEN defaults to seed 1 and
unlocked layers. Controls live in the serialized ProgramStore snapshot and
restore with the structural Program, while UI-only metadata edits do not
publish to the audio thread. The interim UI toggles whole layers and preserves
finer masks loaded from state; generator and mutation read the saved controls.
Local codec/store tests, ASan/UBSan and TSan passed. Processor/editor recall
and invalid-state tests are syntax-only; style, intensity, key, A/B, other
locks and full generator metadata are still not integrated.

## Initial motion-to-gain sources

Two free-running sine LFOs (fixed 0.5 and 1 Hz) and a deterministically seeded
4 Hz sample-and-hold join the macro sources for typed master-gain routes. The
source selector exposes their depth but disables the macro value slider for
these fixed-rate sources. FMOD stores the typed routes and worker WAV evaluates
the same runtime. Local deterministic/bounds, codec and offline DSP parity
tests plus ASan/UBSan and allocation probe passed; processor/UI tests remain
syntax-only. Rates, shape, tempo synchronization, seed editing, more sources
and destinations, general matrix UI, and click-free structural changes remain.
LFO phase is currently free-running in the live processor while offline render
starts from reset, so live-to-WAV equality is not claimed for a running LFO.

## Procedural seed Program selector

The Perform panel can now load any of the 48 existing procedural layer seed
Programs. These are explicitly labeled seed Programs, not curated factory
presets: selecting one replaces only the layer Program, leaving host duration,
output, macros, effects and pattern untouched. Whole-Program replacement also
invalidates stale curve undo history. Generator DSP tests already cover audible
finite renders and structural uniqueness; new editor, automation-precedence
and history tests are syntax-only. Tags, preview/compare, favorites, searching,
curation and perceptual/host acceptance remain unfinished.
