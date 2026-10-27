# Acid production work — 27 October 2026

Owner reassigned PR #59 and Acid DSP/UI to this worker. PR #62 and generic Rack
belong to another agent. This worktree does not edit Rack, the shared ABI or Bass.
The current Acid bridge must be reconciled with the reviewed generic parameter
adapter when Rack is integrated; two independent live parameter authorities must
not remain in the delivered product.

## MIDI ownership correction

Baseline: PR #59 head37c1e10. A fresh actual-AcidEngine regression executable
failed7 of12 initial checks before production edits. A later16-check baseline
failed9, including held-key velocity and unique-key retrigger behavior.

- Eight-entry pitch-only storage evicted held notes. Replace with2048 fixed,
  indexed channel/key nodes and an intrusive last-key priority list. Ordinary
  insertion/removal/fallback is O(1); channel CC cleanup visits128 keys.
- Re-press replaces one channel/key ownership, matching RackRouter's retrigger
  behavior. Background/unmatched NoteOff does not change pitch/glide/gate.
- Retain velocity/accent on fallback. Pitch Bend and CC65 belong to their MIDI
  channel. CC123 releases owned notes; CC120 immediately silences the channel,
  including a voice already in release. Foreign channels cannot cut that tail.
- Require valid three-byte supported channel messages and7-bit data. Events
  outside `[0,sampleCount)` are ignored, never applied early after the block.
  The caller remains responsible for sorted, correctly scheduled event spans.
- No semantic parameter IDs, v8 sound encoding, host IDs or plugin identity change.
  The new note/controller ownership is transient and reset on lifecycle reset.

Verification:1376 checks PASS in the actual engine, including per-block C++ heap
allocation counters. Independent reviewer freshly built and ran the same source
with ASan/UBSan:1376 PASS; LeakSanitizer disabled. This is module coverage, not a
claim of complete Rack callback/host allocation certification.

## Measured blockers still open

1. `tailSamples()` underreports release: maximum-release NoteOff remains active
   after declared86639 samples, measured peak0.113012. Envelope timing, host
   declaration and descriptor budget must agree before production acceptance.
2. Exotic finite sample rates1e-320/1e300 are accepted but produce NaNs. Ordinary
   supported rates need explicit admission limits and render evidence; retain the
   existing high-rate saturation regression rather than silently deleting it.
3. Full Windows Release/CTest/package and real Cubase/REAPER audition, automation,
   project restore and UI/reference acceptance remain mandatory.
4. Acid UI/reference, generator controls/persistence, complete content delivery
   and generic Rack adapter integration remain separate completion gates.

## Execution log

1. Read PR #59 history, scope/control matrix, source and latest Windows run. The
   current refactor's Windows run40544434435 was queued; no green result claimed.
2. Create an isolated Acid worktree at the exact PR head; leave Rack work intact.
3. Add failing behavioral MIDI tests before editing DSP. Reproduce held-key,
   channel, malformed-input and block-boundary failures in actual source.
4. Replace bounded ownership at the owning Acid layer; independently review it.
   Add and reproduce CC120-during-release failure, then fix the release-owner case.
5. Run optimized and fully instrumented ASan/UBSan module tests; all1376 pass.
   Native full-suite verification follows before publication.

ub-mode is available and used in this session. Its named crapper/dryer/mutator
tools are unavailable; no metrics or mutation-test result is fabricated. Existing
older audit text describes that earlier worker's environment, not this session.

Disposition: **DO NOT MERGE / Acid not production accepted**. No DLL loader is
introduced. Static providers remain the version1.0 design; independent content
packs do not require independent runtime binaries.
