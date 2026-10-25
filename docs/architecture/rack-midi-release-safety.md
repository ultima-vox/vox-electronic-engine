# Rack MIDI release safety

Scope: internal RackRouter hardening. No plugin identity, parameter IDs, instrument ABI
or persistent state schema is changed.

## Reproduced failure

The original 27 router checks pass. A new integration-style router scenario fails
against the original implementation:

1. Send NoteOn to a slot.
2. Fill its bounded destination buffer with 2,048 events.
3. Send the one-shot NoteOff, which cannot be pushed.
4. Start the next block with no additional MIDI.

The original code remembers some owners but does not autonomously retry a release.
A failed CC120/123 is worse: it clears source-channel ownership without delivering the panic.
A missing release can therefore leave the instrument sounding until a separate reset.

The same failure mode exists across external, audition and generated paths.
This is a router failure; it is not claimed to explain the owner's Bass timbre complaint.

## Recovery policy

- A failed critical NoteOff, synthetic retrigger NoteOff or routed panic queues the affected SlotId.
- Queue capacity: 16 stable slot identities; duplicate requests coalesce.
- New NoteOns to a pending slot are rejected for the remainder of that block.
- At the next route/block boundary, clear destination buffers and flush recovery first.
- Send CC120 on all 16 MIDI channels, at sample offset zero, to that slot only.
- Clear its ownership across all source domains after recovery is placed in the buffer.
- New same-offset notes follow those resets; the existing stable sort preserves order.
- Reset, module replacement via forgetSlot, and removal of the original SlotId cancel stale recovery.
- Reordering or disabling the same SlotId does not redirect or suppress recovery.
- No heap allocation, locks, file access or unbounded queue is introduced.

This is a **slot-wide emergency silence**, not a transparent musical recovery.
It may cut all notes/tails of the affected slot. Adjacent slots are not reset.
All channels are covered because some providers respect MIDI channel while current built-ins
may reset their entire instrument on a panic.
The invariant is that the slot cannot remain sounding indefinitely because one release was lost.

Recovery is flushed by route(), called once before routeAudition()/routeGenerated()
in Rack::process. Standalone callers must use the same block-boundary ordering.
A recovery storm is bounded to 16 x 16 = 256 reset events across the rack.
It uses the ordinary per-destination 2,048-event budget; no extra event budget is silently allocated.

## Input boundary

The router accepts well-formed MIDI 1 channel-voice messages:
two bytes for program change/channel pressure, three for the remaining voice messages.
Relevant data bytes must be 7-bit. SysEx/system transport messages do not belong to
this fixed channel-voice bridge; host timing is a separate context.
Validation precedes note-array indexing in every source domain.
Invalid SlotId cannot resolve to a default empty slot.
Slot spans are bounded to the output count.
Routing validates mode and 7-bit key/velocity limits.

Audition/generated ownership records the original NoteOn MIDI channel:
changing the channel on a later event cannot send the release to a different sounding voice.

## Verification

- Existing baseline: 27 checks pass.
- First added recovery regression: fails before the fix at the next-block release assertion.
- Expanded router suite: 137 checks, including all source domains, CC120/123,
  reorder/disable/reset/replacement, 16 layered slots, retrigger source switching,
  stale NoteOff, original channels, malformed bytes and oversized slot spans.
- Standalone actual-source compile uses GCC C++20 with -O2 and strict warnings.
- AddressSanitizer/UndefinedBehaviorSanitizer check the same source/tests.
- LeakSanitizer requires accessible process metadata and cannot run in this sandbox;
  leak detection is disabled separately, not address/UB checking.
- Existing instrument-contract and modulation suites provide adjacent-contract regressions.
- Full Windows Release/CTest/package and real DAW checks remain required.

This patch does not claim validated project reopen, sound quality, latency compensation,
or complete production readiness of the rack.
