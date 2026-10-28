# Acid runtime contract

Scope: PR #59 Acid module. Shared Rack, ABI and platform integration remain
externally owned. InstrumentId,77 semantic IDs and v8 sound encoding are unchanged.

## Delivery and parameter ownership

Owner decision 27 October 2026: Acid DSP, specialized UI and content must be
installed independently of the Rack VST3. Sound, Pattern and Generator content
remain distinct; the DAW still loads one Rack plugin. The shared module loader,
ABI, host automation and UI boundaries require review by the Rack owner before
this Acid module can be packaged and connected. Current static Acid integration
is an unmerged implementation candidate, not the delivery architecture.

The current PR's slot-scoped AcidParameterBridge is an integration dependency:
when generic Rack direct parameters land, use its reviewed single authority.
Do not deliver two automation writers or change Rack contracts in this branch.

## MIDI voice policy

Acid is monophonic, last-key priority, with up to four internal unison oscillators.
All2048(channel,note) identities fit fixed indexed storage. Re-pressing the same
key replaces its ownership and moves it to latest priority, matching Rack retrigger
semantics. NoteOff releases that identity once. Releasing the latest key restores
the prior held key's pitch, velocity and accent without a new envelope trigger.
Unmatched/background NoteOff cannot change voice pitch, glide or gate.

Pitch Bend and CC65 belong to their MIDI channel. CC123 releases that channel's
held keys with ordinary envelope release; CC120 silences that channel immediately,
including its already-releasing voice, without cutting another channel's voice.
Lifecycle reset/Panic clears all keys and transient controllers. Runtime key/list/
controller/envelope state is never serialized as part of Sound.

The process input is sorted VoxMidiEventV1. Supported messages have three bytes,
channel status and7-bit data. Invalid messages and offsets outside
`[0,sampleCount)` have no effect. Later scheduling belongs to the caller. No locks,
I/O, construction or heap storage occur in MIDI processing. Ordinary key changes
are O(1); channel-wide cleanup visits exactly128 indexed keys.

## LFO clock during silence

Each free-running LFO advances once per processed audio sample even when the
voice envelope is idle. Host-synced LFOs derive their phase from PPQ. If the host
does not call the plugin, or the instance is suspended/bypassed, free phase is
not advanced; this contract does not invent elapsed time from wall-clock I/O.

## Release, sample rate and resource bounds

Decay reaches Sustain at its selected duration using an exponential curve to
-80dB of the initial distance. Progress accumulates through Decay automation.
When Sustain moves near the deadline, the envelope settles towards the current
target with a 4 ms time constant before entering Sustain; it never jumps to the
new target at the deadline. The selected filter Decay applies to unaccented
notes; accented filter notes retain the engine's separate accent timing.
This corrects a previously overlong decay and changes existing preset sound;
parameter IDs and serialized values are unchanged, and musical audition remains
required.

Release controls express duration to silence, not an exponential time constant.
The released envelope progresses from its captured amplitude to -80dB and then
exact zero by its selected duration. Progress accumulates, so Release automation
changes the remaining decay speed without an amplitude jump. VCA mapping remains
`0.004 + normalized^2 * 1.8` seconds. ENV1/ENV2/filter release use their existing
physical mappings and the same duration semantics.

The module reports a stable conservative VCA bound across Release automation:
`ceil((0.004f + 1.8f) * sampleRate) + 4` samples. Provider sample rates are explicitly
8kHz through384kHz inclusive; maximum tail budget700000 samples covers that range.
Failed preparation leaves the instance unable to process. Other resource bounds
remain unchanged, including64KiB scratch and zero latency.

This intentionally corrects the audible release curve: old code took approximately
9.21 times its displayed duration to reach its idle threshold. Saved values and
wire encoding retain their identity; musical audition must verify the corrected
shorter tails. This is not a compatibility mode or a second DSP implementation.

The standalone C++ engine keeps the existing1e12-Hz saturation diagnostic accepted,
while rejecting below8kHz or above1e12Hz before unsafe floating-point math. That
artificial diagnostic range is not an advertised provider/DAW support range.

## Evidence boundary

See [production evidence](../testing/acid-production-evidence-20261027.md).
Module tests, sanitizers and allocation counters do not establish host automation,
project reopen, target hardware deadlines, audible303 character or visual owner
acceptance. Those remain explicit production gates.
