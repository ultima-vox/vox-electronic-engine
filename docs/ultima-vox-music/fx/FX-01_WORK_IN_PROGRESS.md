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
retriggers. The original interim grid was fixed at 120 BPM; the subsequent
tempo update below supersedes that limitation. There is no slice editor yet. Local tests at 44.1/48/96 kHz and
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

## Fixed-rate phaser

All four serial slots can now select a four-stage stereo allpass phaser. A
0.23 Hz quadrature LFO sweeps a 1 kHz center, and the existing amount parameter
controls sweep depth; wet is host-automatable. Slot changes retain the
existing bounded 5 ms crossfade. FMOD is unrelated; FXSL retains the stable
three-bit slot type encoding and uses the previously unused value 7. One second
of phaser decay is included in offline WAV. Tests cover impulse/stereo response,
block-size invariance, an analytical fixed-notch frequency, finite decay, packed
state, realtime/WAV parity, sanitizers and an allocation probe. Processor/editor
state tests were syntax-checked only. Rate, feedback and tempo sync controls,
chorus, flanger, delay and reverb are still missing. A local Linux diagnostic
96 kHz 128-frame 8-event/4-layer Shepard + four phasers run reported p50 278.32,
p95 300.04, p99 320.12, max 344.37 microseconds; this is not host, Windows or
deadline qualification.

## Trigger-pattern MIDI save action

The panel has an EXPORT MIDI action that captures the current pattern before
opening an asynchronous save dialog. It writes a type-0 MIDI file at explicitly
fixed 120 BPM, containing the deterministic trigger notes (not audio), using a
temporary file and an overwrite-confirming chooser. Cancellation does nothing;
errors are reported. A .mid extension is required instead of silently changing
the confirmed destination. Pattern/scheduler/SMF standalone tests pass; panel
code and its basic control assertion were syntax-checked against JUCE 9.0.1
only. Native dialog, overwrite/error behavior and host interaction still require
Windows runtime testing. WAV file UI, drag export and tempo selection remain.

## WAV save action

The WAV button captures a self-contained Program, FX, macro and base-parameter
snapshot before opening the save dialog, then renders C4 at full velocity,
48 kHz/24-bit on an editor-owned worker. It reports render progress, offers
best-effort cancellation, writes a temporary file in chunks, and joins its
worker on editor destruction. No worker holds a processor/editor pointer.
Cancellation before replacement leaves the target alone; cancellation racing
the final replacement may be too late. File writes and the destructor join can
block on a slow filesystem, but not on the audio callback. Offline render tests pass. A JUCE-core Linux runtime test now exercises actual
worker rendering, byte equality, write failure, cancellation, target preservation
and teardown; its previous form also passed ASan/UBSan. Controller capture and
gui/dialog integration are syntax-checked only. Windows file chooser, worker,
cancellation, overwrite, teardown, write failure and host tests remain required. Fixed
format/rate/note, no drag export, and live free-running LFO phase mismatch
remain explicit limitations.

MIDI and WAV use the same temporary-file writer, which checks output length
before replacement. MIDI writing currently happens on the message thread and
may block on a slow filesystem; WAV rendering/writing is off-thread. Neither
the test nor temporary-file replacement establishes crash-atomic durability
on every filesystem.

## Gain and stereo-balance modulation

Macro, fixed LFO and sample-and-hold sources can now target master stereo
balance as well as master gain, including both destinations from the same
source. Pan is a post-effects linear balance: center preserves both channels,
and the extremes mute the opposite channel; it is not per-layer panning or an
equal-power mono pan. FMOD stores typed destinations and the panel selects
gain or pan; neither route overwrites host automation. Standalone codec, DSP,
WAV parity, ASan/UBSan and zero-allocation probe pass. Processor/UI/state tests
are syntax-only. Pitch/cutoff destinations, other sources, route-wide editor,
and click-free route reconfiguration remain unfinished.

## Fixed-rate stereo flanger and slot packing migration

Each slot can now select a short modulated stereo delay with fixed 0.25 Hz
quadrature LFO and 0.35 feedback; amount controls sweep depth. Two preallocated
delay banks per slot allow existing 5 ms type crossfades. Banks are allocated
on prepare and lazily invalidated on reuse, without callback clearing,
allocation or deallocation. Failed/unprepared bank allocation produces safe
bypass for the flanger; prepare failure is not yet surfaced as host telemetry.
Offline rendering includes one second of flanger decay. Internal packed slot
words now use 4-bit type and 6/6-bit auxiliary values; the host-automated wet
and amount parameters retain their precision. FXSL v1's legacy 3/6/7 layout
is explicitly migrated and FXSL v2 remains the persisted current format.

Standalone impulse, first-input/warmup, stale-bank, block invariance, decay,
8–384 kHz boundary, WAV parity and allocation/transition tests passed; selected
ASan/UBSan passed. Processor/UI, FXSL migration and host behavior remain
syntax-only locally. A Linux 96 kHz, 128-frame diagnostic 8-event/4-layer
Shepard + four flangers reported p50 316.96, p95 356.22, p99 451.27, max
464.30 microseconds; this is not host/Windows deadline qualification. Delay,
chorus, reverb, rate/feedback controls and tempo sync are not yet implemented.

## Chorus and finite single-tap delay

Four slots now also offer a stereo chorus (fixed quadrature 0.22 Hz, amount
controls depth, no feedback) and a single-tap stereo delay (amount controls
50–500 ms, no feedback or tempo sync). Both use the same prepare-allocated
double banks; the maximum buffer memory is approximately 12.3 MB at 384 kHz
for the four shared slots. Existing bypass/wet and 5 ms structural transitions
apply. Offline WAV includes up to 0.5 seconds for each serial delay plus one
second for other decaying FX; four 500 ms delay slots have a tested 2-second
tail. Host tail reporting is conservatively increased to 35 seconds for the
32-second event and up to 3 seconds of effect allowance. A formal full-range
worst-case tail proof and actual host Stop/bounce behavior are still needed.

Standalone impulse timing, serial tail, chorus stereo response, WAV tail and
parity, transitions, allocation and ASan/UBSan tests passed; JUCE-core file
export test passed after the change. Processor/UI/host state tests are syntax
only. In a diagnostic Linux 96 kHz 128-frame 8-event/4-layer Shepard run, four
delays reported p50 340.07, p95 488.56, p99 605.12, max 791.28 microseconds;
four chorus slots p50 363.11, p95 532.68, p99 568.92, max 591.88 microseconds.
These are not Windows/host or deadline qualification. Reverb, multiple delay
taps/feedback, tempo sync, adjustable chorus controls, host testing, and
allocation-failure reporting remain.

## Bounded multitap reverb

Each slot can select a basic four-tap-per-channel stereo feedback reverb.
Amount changes feedback between 0.1 and 0.4; this is an initial algorithm, not
a claim of production-quality room modeling. After two seconds below the
input silence threshold, its bank is logically retired without callback
clearing. Offline tail allowance adds two seconds per serial reverb (plus
delay/short-effect allowances); host reports 41 seconds to conservatively
cover a 32-second event and up to nine seconds of effects. Four serial reverb
impulse decays were checked at 8 kHz within eight seconds. Full-range tail
proof and real host Stop/bounce behavior remain unverified.

Standalone stereo impulse, block-invariance, finite tail, reverb/delay WAV
parity, type packing, ASan/UBSan and transition allocation tests pass.
Processor/editor recall is syntax-only. A diagnostic Linux 96 kHz 128-frame
8-event/4-layer Shepard + four reverbs run reported p50 333.63, p95 503.67,
p99 557.15, max 640.84 microseconds; this is not host/Windows qualification.
More perceptual validation, room/damping/predelay controls, tempo sync,
feedback-delay options and automated quality policies remain.

## Session comparison A/B

The comparison menu stores two independent full VXFX state snapshots in the
processor for the lifetime of the instance, so reopening the editor does not
lose them. Explicit Recall A/B uses the validated project-state restore path
and notifies the host of restored parameter bases (including automation-linked
values); this is stated in the menu and tooltip. These are session-only slots,
not persisted as part of a project, and are not a curated preset browser.
Processor/UI tests cover empty recall and independent Program/effect/route/
pattern/parameter snapshots but have only been syntax-checked locally; runtime
host notifications, automation interaction and GUI layout need verification.

## Basic audio telemetry

A lock-free audio-thread snapshot publishes active event count (including the
one bounded retiring fade voice) and occupied grains once per block. A separate
lock-free latch catches output peaks at or above full scale between UI polls;
the Perform meter shows a one-second clip hold and clamps only its visual bar,
not audio. Granular tests exercise 0/8/9 events, grain retirement and panic,
including allocation probe and ASan/UBSan. Processor and meter tests remain
syntax-only. Counters are sampled at block boundaries, not continuous gauges;
CPU/memory/dropped-trigger metrics and host/visual runtime checks remain.

## Sample ownership foundation (not integrated)

An isolated three-version immutable mono sample store admits validated finite
[-1,1] content at 8–384 kHz, at most 4,194,304 frames per version. Controller
publication allocates and retires storage; audio acquisition uses one bounded
CAS and explicit leases, with no callback allocation, lock or destruction.
Outstanding leases keep old versions alive across replacement and clear; if
all slots are held, publication fails rather than evicting active samples.
A failed acquisition does not imply a fallback. Explicit lease release and
Store lifetime beyond all leases are mandatory integration contracts.
Two-reader concurrent publication stress passed TSan, as did standalone
validation, slot-exhaustion and allocation probes; an earlier test form passed
ASan/UBSan. This is **not** user sample support: decoding, hash/path manifest,
missing-file policy, audio event/grain lease ownership, sample-rate-correct
playback, UI, WAV capture and project persistence are all still unimplemented.

## Bounded sample WAV decoder (not integrated)

A worker-only RIFF/WAVE reader decodes mono/stereo PCM16/24/32 and IEEE
float32, validates normalized finite samples, preserves source sample rate,
downmixes stereo to mono, caps input at 64 MiB / 4,194,304 frames, and hashes
the exact source bytes with JUCE SHA-256. Decode/read failures leave the
previous result unchanged; shorter/extended files are rejected, while
same-length concurrent edits cannot be detected. Strict RIFF size, alignment
and channel checks intentionally reject unsupported WAV variants (including
WAVE_FORMAT_EXTENSIBLE), not silently reinterpret them. Linux JUCE-core/
cryptography execution tests cover PCM formats, float, a known digest,
malformed input sweep, disk/missing-file paths, store publication and
ASan/UBSan. This decoder and store are still **not** wired to the instrument:
asset manifest/path/hash state, missing-file policy, sample-rate-correct
granular event/grain ownership, UI and offline capture remain unfinished.

## Opt-in sample-backed granular engine path

NoiseRiser can now attach a longer-lived sample store while stopped and an
explicit per-note flag selects user sample playback; missing/racing content
rejects the event instead of falling back. Each event holds an immutable lease,
transferred to the retirement voice on steal and released after its event ends
or reset. Grain playback includes source/output sample-rate ratio and handles
short buffers/high rates; built-in procedural selection remains the default.
Standalone tests cover replacement while events are active, new-event content,
a 24-to-48 kHz spectral pitch check, extreme single-frame source rates, steals,
panic, explicit fallback, concurrent writer/renderer TSan, ASan/UBSan and
allocation probe. Existing riser/layer/granular/offline/store regressions pass;
processor/editor code was syntax-checked only.

This does **not** expose user sample import in the product yet. Processor
lifetime, worker/UI load, project manifest/hash validation and missing-file
policy, sample selection persistence, and offline sample snapshot/render are
still required before exposing a control. The caller must keep the store alive
longer than the engine and call attach only while stopped.

## Sample reference and asynchronous controller foundation

A versioned, bounded UTF-8 path/SHA-256 reference codec and a worker-owned
sample controller now support asynchronous imports and hash-verified restore.
A restored external reference is marked unavailable immediately until verified;
a missing/mismatched file never falls back silently. Failed new imports retain
the prior selection, superseded work is discarded, and audio can require the
exact immutable publication generation. Standalone JUCE-core worker tests cover
file import, failure, mismatch, restored identity, supersession and held leases;
ASan/UBSan and TSan passed. The controller and reference codec are not yet
wired to processor state, UI, MIDI triggering or offline capture. Worker teardown
may wait for filesystem I/O, so it must never occur on the audio callback.

### Processor sample integration (unqualified)

FSMP v1 stores a validated path and byte SHA-256. Restore validates all sections
before scheduling a hash-checked worker request; absent/mismatched content uses
an explicit unavailable generation, never the built-in source. Processor note-on
uses the exact selected generation, and captured WAVs copy the leased sample to
worker-owned memory. The controller is declared before the engine so it outlives
all voice leases. Standalone sample-backed WAV comparison passed normally and
under ASan/UBSan. Processor and test code passed JUCE syntax checks only: the
processor/state/A-B/import tests have not run here. An interim sample import/built-in selector is now wired, but native chooser,
status presentation and processor tests have only been syntax-checked here. File-system restore and destruction may block non-audio host threads;
real host callback/lifecycle and concurrent restore behavior need qualification.
A missing external selection suppresses only events with an enabled granular
layer; mixed events fail as a whole rather than silently substituting content.
Non-granular event and WAV rendering continue.

### TL P1 follow-up: source and pending-import contract

FSMP v2 adds an unverified pending-path kind; v1 built-in/external references
still decode. Pending import immediately selects unavailable and saves that path,
not the previous playable reference. A/B or project restore of that pending path
remains unavailable (it does not auto-trust an unhashed file). A failed import
rolls back the pre-import selection; superseding imports retain the same rollback
point. Restores supersede in-flight work. Status `failed` signals the failed
request even when the rolled-back selection is playable. A gated deterministic
test exercises pending save, supersession and rollback. A concurrent standalone
loader/restore/MIDI-engine stress test passed TSan; granular, offline and
controller tests passed, including selected ASan/UBSan. Actual JUCE processor,
A/B, Windows and host execution still require exact-head qualification.

### Interim sample UI

The Perform panel can choose a user-supplied WAV asynchronously or explicitly
select the built-in source; it reports loading, ready, missing and failed with
the selected path in a tooltip. The chooser asks for licensed user content; no
samples are bundled. The native chooser, visual layout, editor lifecycle and
processor/A-B state tests remain unexecuted locally and need Windows/host checks.

The concurrent standalone loader/restore/MIDI-engine stress test also runs the
thread-local allocation probe around note-on, render and panic; Linux probe
reported zero allocations. This does not replace allocation measurement of the
JUCE processor callback or Windows host execution.

### Layer pan curve editor

Each layer's existing curve editor can now select gain or pan. The selector
edits the actual Program curve, with independent selection and undo history
cleared on switching; whole-Program replacement still clears stale undo. A
standalone DSP/codec test confirms that a saved pan sweep moves energy from left
to right and passed normally and under ASan/UBSan. Editor/processor regression
tests are added but only syntax-checked locally. Pitch, cutoff and global curves
remain unimplemented.

Curve mouse/key handlers now check the Program replacement epoch before editing,
refreshing the displayed curve and cancelling an in-progress drag on observed
replacement. Added a JUCE regression for replacement during a drag; syntax
checked only. ProgramStore now publishes a replacement revision under its writer mutex; curve
edits compare that revision while holding the same mutex. A standalone concurrent
stale-edit/restore test passed under TSan and ASan/UBSan, and the audio snapshot
allocation probe passed. Actual JUCE processor/host concurrency still needs
qualification.

WAV chooser now retains one shared capture and moves it into the render worker,
avoiding extra copies of large user-sample buffers. Missing captures fail before
opening the chooser, and allocation failure while copying a lease releases it
and leaves an explicit unavailable capture. This path is syntax-checked; native
chooser and out-of-memory behavior remain untested in a host.

### Generator controls

The interim generator settings panel now binds style-seed variant, layer count,
intensity, energy and stereo spread to the real deterministic generator. FGEN v2
stores these controls along with seed and locks; v1 migrates to the previously
hard-coded UI defaults. Program family remains the selected family and host
duration is not overwritten. Standalone state/migration/generation tests passed
normally and under ASan/UBSan; JUCE UI/project roundtrip tests are syntax-checked
only. Style is currently a deterministic seed variation, not a curated musical
genre. Pitch/key, bars, full sound-design controls and curated presets remain.

The generator settings overlay also exposes per-layer family, enable, timing,
gain, pan, gain-envelope and pan-envelope locks; they modify the already
serialized lock masks, while the top-level layer toggle means all fields locked.
The processor/editor tests for selective lock mutation and save/restore were
added and syntax-checked only; no host/UI execution is claimed. Locks for
not-yet-implemented Program fields, pattern, effects and sample remain open.

### Glitch tempo follow-up

The event-local sixteenth grid now integrates host BPM per sample, preserving
phase across block-level tempo changes. Invalid or absent host BPM uses 120 BPM;
playing/stopped does not itself alter the tempo selection. The grid is not
aligned to host PPQ, and changing tempo within one host block cannot be observed
through the current wrapper. Offline WAV captures the latest observed tempo and
holds it constant. Standalone gate/tempo-change, block-partition, offline parity
and allocation-probe tests passed; selected ASan/UBSan passed. Processor host
tempo tests and editor passed syntax checks only; real host transport and
click/listening tests remain open. Noise-riser reference tests remained within
the previous tolerance. A concurrent Linux run showed a single 8.08 ms maximum
for the 48 kHz nine-voice 128-sample benchmark (p99 82.45 µs); this exceeds
the 2.67 ms period and is not production timing qualification or evidence of
a root cause. Repeat controlled p50/p95/p99/max measurements on Windows.

### PPQ-only transport discontinuity fallback

Transport tracking uses host sample positions when continuous sample data is
available. If not, it predicts PPQ from the previous block's valid tempo and
sample rate and fades active events for jumps of at least 0.125 quarter notes
(a 1/32 note). This threshold tolerates host rounding and small unreported
intra-block tempo changes but deliberately cannot detect smaller PPQ-only seeks.
Stop fades, absent/invalid host information resets only the corresponding
prediction, and a completely unavailable position resets tracking. Standalone
tests include stop, sample precedence, PPQ seeks, tempo changes, jitter, invalid
values and integer overflow; ASan/UBSan and Linux allocation probe passed. JUCE
processor integration was syntax-checked, not executed, and Cubase/REAPER host
behavior remains unqualified.

### MIDI export tempo

MIDI export now captures the latest valid host BPM with the pattern before the
save chooser opens, using 120 BPM if no valid host BPM was observed. BPM outside
the MIDI scheduler's 10–1000 range fails explicitly instead of being silently
replaced; the exported type-0 file has a fixed tempo and 4/4 time signature.
Standalone SMF tests verify encoded tempo and canonical scheduling, including
ASan/UBSan. Processor/editor tempo tests are syntax-checked only; native file
chooser and real host tempo behavior remain unqualified. This supersedes the
earlier fixed-120-BPM export description.

### Bounded pattern playback

The realtime host scheduler now checks at most eight pattern cycles per block,
including its one-sample look-behind. It rejects an oversized block before
emitting any pattern notes instead of iterating in proportion to an arbitrary
host block length. The audio callback reports unavailable pattern timing to the
interim editor for missing/unsupported host timing or scheduler rejection; live
MIDI still plays. This bounds work, but even the allowed worst case can scan
12,288 schedule events and needs real callback profiling. Standalone block
parity, huge-block rejection, ASan/UBSan and Linux allocation probe passed.
Processor/UI status tests were added and syntax-checked only; they remain to
be executed on Windows and in hosts.

A standalone stress benchmark now times maximum pattern scheduling with
eight 4-layer Shepard events and four drive effects at 44.1/48/96 kHz. It
records p50/p95/p99/max, dropped triggers and saturation rather than declaring
a deadline pass. This extreme 1000-BPM run exposed many dropped triggers during
the single retirement fade and sustained >=1 output with four drive slots; see
`DSP_BENCHMARK.md`. Real Windows plugin/host timing and musical acceptance remain
open.

Pattern enumeration skips irrelevant sorted events with bounded binary search
and exits at the block end, retaining the eight-cycle cap. A 5000-case
full-scan-reference comparison plus grid tests, ASan/UBSan and allocation probe
passed. The DSP stress run preserved checksums/drop counts; noisy timings do
not prove a speedup or host deadline. See `DSP_BENCHMARK.md`.
