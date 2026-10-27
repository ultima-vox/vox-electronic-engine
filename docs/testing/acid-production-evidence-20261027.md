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

## Release duration and numeric admission correction

New94-check actual-engine test first failed17 checks: every tested Release duration
at44.1/48/96/192/384kHz exceeded its displayed deadline, and two unsafe finite sample
rates were accepted. The corrected exponential reaches -80dB then exact zero at
its selected duration. Continuous progress keeps automation amplitude continuous.
The stable declaration uses the maximum VCA duration+4 samples, not mutable DSP
parameter state; the provider's700000-sample budget covers its explicit8k..384kHz
range. A failed prepare disables processing. Existing1e12-Hz direct-engine tail
saturation coverage is retained, separate from provider support.

Optimized release96 and MIDI1376 tests PASS after this correction. The audible
curve intentionally changes; v8 encoding/77 IDs do not. See the
[runtime contract](../acid/ACID_RUNTIME_CONTRACT.md). Independent review and full
native/Windows results must be recorded separately before acceptance.

Freshly compiled current AcidEngine, AcidProvider and AcidTests, linked to the
already-built unchanged JUCE/Sequence/compliance dependencies, pass81876 checks
plus667 compliance checks. This controlled relink is not a full clean-tree build.
The final MIDI1376 checks also pass under an actual1MiB stack. Independent fresh
ASan/UBSan release96 checks PASS; an actual Provider+Engine probe passes11 cases
covering supported rates, budget, failed preparation and recovery. Guard found no
blocker in the timing/budget/numeric fix. LeakSanitizer is disabled. The full native
build/CTest is not yet complete.

## Measured blockers / acceptance still open

1. The independently measured pre-fix tail86639/peak0.113012 and exotic-rate NaNs
   have source/test corrections above. Independent verification, supported-rate
   provider budget checks and fresh Windows validation remain required.
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
6. Reproduce the release deadline and rate failures in94 actual-engine checks.
   Correct the envelope semantics and declared/provider bounds together; retain
   the existing high-rate regression. Document the audible behavior change.

ub-mode is available and used in this session. Its named crapper/dryer/mutator
tools are unavailable; no metrics or mutation-test result is fabricated. Existing
older audit text describes that earlier worker's environment, not this session.

Disposition: **DO NOT MERGE / Acid not production accepted**. No DLL loader is
introduced. Static providers remain the version1.0 design; independent content
packs do not require independent runtime binaries.
