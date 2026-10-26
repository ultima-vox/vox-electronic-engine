# Rack audio routing v1

Candidate implementation on `feat/rack-production`; related #53–#57.
This changes shared Rack infrastructure, not the Acid instrument or its UI.

## Fixed topology and identities

Sixteen immutable automation slot banks feed Main and four internal FX returns.
Return bus IDs are `0x56585201` … `0x56585204`; there are exactly 64 slot-to-return
edges. No return-to-slot/return-to-return edge exists, so feedback cycles cannot
be expressed. Host buses remain mono/stereo Main; no external audio sidechain or
additional VST3 output is claimed. Existing MIDI ownership is unchanged.

Host parameter IDs:

- `slotNN.send.B.level` (0–1), `slotNN.send.B.pre` (boolean), NN=01…16, B=1…4.
- `rack.return.B.type`, `.enabled`, `.level` (0–2), `.pan` (-1…1),
  `.amount`, `.rate`, `.character` (0–1).
- `rack.duck.source` (Off / immutable slot bank 1…16), `.note` (0…127),
  `.attack` (0…100 ms), `.hold` (0…500 ms), `.release` (1…2000 ms).
- `slotNN.duck.depth` (0–1).

The source bank resolves to that bank's current persistent SlotId before the
Rack callback. Display selection does not change routing. Slot-order drag/reorder
is not implemented; any future reorder must move identity-linked routing and
host bindings together through a reviewed contract.

APVTS owns these plain base values; the audio callback reads cached atomics into
a fixed block snapshot. Presets/project state capture APVTS, Rack and global FX;
return delay memory, duck phases and meter peaks are runtime only. They reset
on successful project/scene restore, prepare, release and Panic. A failed restore
must retain the previous committed scene. No live DSP is serialized.

## Signal policy

A slot's output receives ducking, then its level/pan. Pre-fader sends tap after
ducking and before level/pan; post-fader sends tap after level/pan. Disabled,
muted and solo-excluded sources contribute no dry audio or new sends. Returns
continue existing tails when an input is muted or solo changes; a shared return
cannot retrospectively separate contributions already in its delay memory.

Each return selects one of the existing eight effects at 100% wet. Return off is
silent; it never becomes a parallel dry path. Effect-type/off transitions clear
the old tail. Return level/pan act after the effect. The existing global chain
still follows the complete Rack mix, and master Output follows that chain.
Master gain ramps for 5 ms and never rewrites an instrument base parameter.

Current return effects have zero declared latency. This change supplies no
new lookahead or compensator for future nonzero-latency parallel processors.
Module latency policy/PDC must be reviewed before admitting such processors.

## Kick-event ducking

This is an explicit routed MIDI note-lane trigger, not an audio detector. Choose
one source slot and its routed note (after the source's transpose). Only a positive
NoteOn for that lane triggers it. Mute/disable/solo-exclusion, missing source or
source/note changes clear the envelope. The source itself is exempt from ducking.
A future kit must expose a distinct kick lane; arbitrary notes/claps must not
silently be treated as kick. Drums DSP is not introduced by this implementation.

The attack rises linearly from current envelope to 1; retrigger continues from
the current envelope. Hold keeps 1, release falls linearly to exactly 0. Zero
attack triggers on the exact event sample; zero hold introduces no extra hold
sample. Phases continue across blocks. Recipient gain is `1 - depth * envelope`;
base Output, pitch, velocity and ADSR parameters remain unchanged. The return is
not ducked again, avoiding double attenuation of wet audio.

## Resource and realtime boundary

Preparation accepts 8–384 kHz, 1–65536 samples and 1–2 channels. Four preallocated
EffectsChains, four stereo return buffers and one envelope buffer are prepared
outside processing. Invalid preparation fails closed. Blocks validate readiness,
size, channel shape, pointers, finite/ranged settings and bounded event counts.
No topology construction, allocation, locks or file access occurs in this stage.

Effects ring reset is O(1): sequential writes and valid-prefix masking treat
unwritten data as zero. Skipped prepared channels receive zero writes, preserving
that invariant through mono/stereo processing without reprepare. Meter publication
uses one bounded atomic compare/exchange attempt; it may drop a racing peak, never
retry in a realtime loop.

These bounds and scoped allocation checks do not establish a product CPU/RAM
promise. Measure the declared 16-slot × four-send × four-return workload; agree
target hardware/buffer size and perform the integrated Cubase/REAPER milestone.

## Shared UI boundary

The accepted red reference consists of the top header and left 16-slot rail.
It supplies full-scene preset navigation/save, editable seed/dice, MIDI/CPU/stereo
output telemetry, master Output, Panic, Settings, selected slot, independent power
and catalog load/replace/unload. Slot context menus expose mute/solo and explicit
MIDI channel assignment with conflict errors. Settings opens the Rack audio
routing utility and source/size/about choices. The green Acid workspace is owned
by its separate agent; this task does not draw or implement it.
