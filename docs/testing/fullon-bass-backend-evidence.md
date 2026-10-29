# Full-on Bass backend evidence

Status: engineering comparison, not a production acceptance or a final backend
decision. Owner requires graph-first `.vins`; native is an optional exception,
not a default consequence of Acid's native implementation.

## Shared graph versus existing Bass

The shared graph at PR #76 (`6a12545898b7c93fa3f1bb29f3b3b5a12f0fb47d`)
offers a polyBLEP oscillator, ADSR, TPT state-variable filter, VCA and gain.
FullOnBass uses a pitch envelope with amount, time and curve; a driven oscillator
with gain compensation; pre-filter nonlinear drive; an RBJ resonant biquad with
note-dependent key tracking; note-on ADSR parameter snapshots and retrigger;
and output gain smoothing. Graph oscillator pitch, filter model, retrigger and
gain behavior do not currently reproduce those operations. Graph Demo Synth
parity tests compare the graph to Demo Synth, not FullOnBass.

The graph cannot yet express a demonstrated sound-equivalent FullOnBass patch.
A native exception is a reasonable provisional integration candidate while
the shared owner evaluates reusable primitives, but it is not justified as a
final shipping decision without graph implementation, measured waveform and
spectrum, pitch/filter response, release behavior, CPU comparison and owner
listening. No graph parity measurements are claimed here.

## Existing DSP evidence

Windows 11 Pro 26100, Xeon E5-2680 v4, Release build. The optional
`VOX_BASS_CPU_CSV` test mode measures 200 warmed blocks per combination of
44.1/48/96 kHz, 64/128/512 samples and 1/2/8/16 independent Bass DSP instances.
It records p95, p99 and worst elapsed processing time plus p99 block-budget
percentage. This is a short, shared-machine DSP-only probe: it does not include
Rack, host, UI, mixing, stress or graph processing and is not a CPU guarantee.
At 96 kHz / 64 samples the observed p99 block-budget percentages were 1.8%,
14.175%, 9.105% and 15.45% respectively; the non-monotonic figures indicate
scheduling noise. Preserve the complete CSV with the test artifacts.

The optional `VOX_BASS_RELEASE_WAV_DIR` test mode writes 32-bit float mono dry
renders from the real engine at 48 kHz, 256 samples, with a 250 ms release and
event at absolute sample 5184. NoteOff and CC123 renders matched bit-for-bit;
their release reached silence after 12001 samples. CC120 was silent from its
event offset. The NoteOff/CC123 sample jump at the event was 0.0025914; CC120's
immediate-stop jump was 0.1348017. These controlled renders do not reproduce or
explain an unspecified DAW project reporting a sustained beep.

## SOUND surface

The current independent engine has 13 stable parameters in `fullOnParameters`.
The historical `BassPanel` uses legacy APVTS IDs and cannot be the final direct
editor. The #43 visual workspace displayed 28 knobs, many explicitly marked
preview-only; it does not establish that all 28 are approved independent DSP
controls. The owner-approved functional reference and the Rack-owned editor
contract are required before adding controls or claiming UI acceptance.

## Shared-owner integration requirements

Rack must supply the canonical V2 header in the integrated source/build when
the optional adapter is used; the branch currently tests against #80's header
externally and does not vendor it. Rack also owns the host pool, direct control
binding, content API, approved SOUND editor, `.vins` creation/signing, scheduler,
MIDI export and production release. Bass supplies the DSP, descriptors, tests
and Bass-specific content. No alternate loader or sequencer is introduced.
