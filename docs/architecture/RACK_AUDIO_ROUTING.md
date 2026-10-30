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

Each route snapshot owns `routeSlotIds + sends + depth`. Nonzero duplicate IDs
and active zero-ID routes are rejected. Mixing resolves recipients by SlotId,
so a reordered render span cannot reassign a send or duck recipient. Missing
recipients receive no substitute route.

The source bank resolves to its immutable project bank-to-SlotId mapping before the
Rack callback. Display selection does not change routing. The host bridge reads the bank snapshot, never the display/render ordering. Slot-order drag/reorder
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

Current modules/returns have zero latency; the production registry rejects
nonzero-latency descriptors pending reviewed parallel-path compensation.
FX schema 2 bounds reverb history to 8 s, delay to 6 s, phaser to 0.5 s,
flanger/chorus to 0.1 s and bitcrusher to 0.01 s after the last nonzero original
input sample. The final min(50 ms, declared tail) fades to zero; expiry resets
histories at the exact sample, including new input later in that block. Tiny
nonzero input retriggers the deadline; no hidden noise gate is used. Serial
stateful stages share the chain deadline. Reverb feedback row gain is at most
0.91. Bypass/disable clears the affected stage history. This is an intentional
finite-tail policy, not a claim that every natural decay is inaudible by then.
The host tail getter reads one atomic duration; it never queries mutable DSP.
See [parameter/state contract](RACK_PARAMETER_CONTRACT.md).

## Kick-event ducking

This is an explicit note-lane trigger, not an audio detector. Choose one source
slot and its kick note.

**Triggers:**
- a positive routed MIDI NoteOn for that note (after the source's transpose);
- a note of that number that the source generated itself in the same block (its internal sequencer, reported through the module note output, see [PATTERN_CONTENT_ABI](PATTERN_CONTENT_ABI.md)).

When a routed and a generated kick land on the same sample, they trigger once.

**Routed kicks the source did not play** (PROPOSED, V2.2, awaiting owner
approval on #86). A source that reports `VOX_ROUTED_NOTES_ALL_IGNORED` for a
block (validated by the native adapter) has none of that block's routed NoteOns
count as triggers; only its generated notes do.
- Full-on Drums reports this in Internal mode, so a stray kick clip neither
  sounds nor ducks. Host mode keeps routed kicks; Both combines both sources,
  and a coincident pair triggers once.
- A legacy module, a voided or bypassed block, and any undefined report read as
  `MAY_PLAY`: routed kicks trigger as before.
- The value covers a whole block. The Rack sends parameter changes only at
  block boundaries, so a mode switch takes effect on the next block. A module
  that sees a mid-block change of what it admits reports `MAY_PLAY`.

**What clears the envelope:** mute, disable or solo-exclusion of the source, a missing source, and source or note changes. A silent source ducks nothing, and the source itself is exempt from ducking.

**Same-block, two-pass processing.** The Rack renders every slot into its own buffer first. It then builds the block's envelope from the routed MIDI and the source's generated notes, and only then mixes. As a result:
- an internal kick at sample *k* ducks its targets at *k*, whether the source slot comes before or after them;
- block size and sample rate make no difference;
- there is no lookahead and no added latency.

**Generated events from the module are checked by the host:**
- out-of-order or out-of-block events are ignored;
- a count above the buffer capacity voids that block's events.

A kit must still expose a distinct kick lane; arbitrary notes or claps are never
treated as a kick.

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
and catalog load/replace/unload. Slot context menus expose mute/solo and the MIDI
channel picker: one instrument = one channel, channels owned by another slot are
disabled, a loaded instrument takes the next free channel. Settings opens the Rack audio
routing utility and source/size/about choices. The green Acid workspace is owned
by its separate agent; this task does not draw or implement it.

## Routing parameter defaults

All continuous routing controls below are plain host units. Gain/pan/mix controls
use step 0.0001; time controls use 0.1 ms; note uses step 1.

| Endpoint | Range / unit | Default |
| --- | --- | --- |
| slotNN.send.B.level | 0..1 linear gain | 0 |
| slotNN.send.B.pre | Boolean | false (post) |
| slotNN.duck.depth | 0..1 reduction | 0 |
| rack.return.B.type | Distortion, Wavefolder, Phaser, Flanger, Chorus, Bitcrusher, Delay, Reverb | Reverb / Delay / Chorus / Phaser for B=1..4 |
| rack.return.B.enabled | Boolean | true |
| rack.return.B.level | 0..2 linear gain | 0.5 |
| rack.return.B.pan | -1..1 | 0 |
| rack.return.B.amount/rate/character | 0..1 | 0.5 each |
| rack.duck.source | Off, immutable slot bank 1..16 | Off |
| rack.duck.note | 0..127 MIDI note | 36 |
| rack.duck.attack | 0..100 ms | 1 ms |
| rack.duck.hold | 0..500 ms | 40 ms |
| rack.duck.release | 1..2000 ms | 120 ms |
