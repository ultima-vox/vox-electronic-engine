# VOX Drums — production candidate (DRM-05, DRM-06, DRM-07, presets)

**Status:** implemented and covered by automated tests; **not** accepted in a DAW.
DRM-08 stays on HOLD until the owner completes
[DAW_ACCEPTANCE_CHECKLIST.md](DAW_ACCEPTANCE_CHECKLIST.md).
**Requirement source:** the owner's assignment and the Song Mode contract of PR #124
(`DRM-05_SONG_MODE.md`, not merged; treated as the requirement).
**Not part of this product:** the Rack, the `.vins` runtime, the Rack ABI.

## 1. Feature checklist

| Feature | Source | Tests |
|---|---|---|
| Song model: 16/32/64 bars; a bar is empty, a reference (slot, A/B, pattern bar) or unique | `products/drums/PatternStore.h`, `SongModel.cpp` | `DrumsCandidateTests.cpp` `testState`, `testSongParity` |
| Song scheduling by the canonical rule (`kit::songHits`, the same cell rule as `stepHits`) | `products/drums/engine/KitEngine.cpp` | `DrumsKitEngineTests.cpp` `testSongScheduler`, `testSongParity` |
| Timeline: select, place, insert, delete, duplicate, clear, copy/paste, Make Unique, loop region, length, mode, playhead | `DrumsPages.cpp` (`SongStrip`), `SongModel.cpp` (`songedit`) | `testEditor`, `testState` |
| Editing a song bar in the step grid (copy-on-write: the bar becomes unique) | `PatternEditor.cpp` (`editSongBar`) | `testEditor` |
| Undo/redo of pattern, song and generator actions | `PatternStore.cpp` (`PatternHistory`), `Processor.cpp` (`editSong`, `generate`) | `testGenerator`, `testState`, `testEditor` |
| MIDI export, scopes Pattern and Entire Song; one frozen snapshot, one renderer for click and drag | `Processor.cpp` (`captureExport`, `renderExport`), `PatternExport.cpp` | `testSongParity`, `testExport` |
| Generator: 7 styles, 8 amounts, seed; pattern, song, mutate lanes, fill, variation; step and lane locks | `Generator.cpp` | `testGenerator`, `testEditor` |
| Mixer: 16 channels (level, pan, mute, solo, peak meter), sends 1/2 | `Processor.cpp`, `engine/KitEngine.cpp`, `DrumsPages.cpp` (`MixerPage`) | `testOutputs`, `testEditor` |
| Shared delay and reverb returns | `engine/SendFx.cpp` | `testOutputs` |
| Main + 8 assignable stereo outputs | `Processor.cpp` (`isBusesLayoutSupported`, `processBlock`) | `testOutputs` |
| Editor: PERFORMANCE, SOUND, PATTERN/SONG, MIXER, GENERATE, ROUTING, PRESETS, ADVANCED | `DrumsEditor.cpp`, `DrumsPages.cpp`, `PatternEditor.cpp` | `testEditor`, `DrumsProductTests.cpp` `testEditorBindings` |
| Presets (kit, pad, pattern, song), Undo Load, portable kit | `Presets.cpp` | `testPresets` |
| Factory content: 3 kit families with patterns and a song | `FactoryContent.cpp`, [FACTORY_CONTENT.md](FACTORY_CONTENT.md) | `testPresets` |
| State sections DSNG, DGEN, DUIS; DSEQ v3; DPAD v2 | `SongModel.cpp`, `Generator.cpp`, `Processor.cpp`, `PatternStore.cpp`, `KitStore.cpp` | `testState`, `DrumsProductTests.cpp` `testState` |
| Callback timing report | `tests/suite/DrumsCallbackBench.cpp` | (reports, does not gate) |

## 2. Song mode

- Time: 4/4. Song bar *B* starts at quarter note 4·*B*. Step *s* of a bar with *n*
  steps is at 4·*B* + *s*·4/*n*, plus swing on odd steps, plus the cell's microtiming
  and ratchets, by the one cell rule shared with Pattern mode.
- Probability: decided by the bar's seed, the **absolute** bar and the step. The same
  song, seed and position give the same notes in playback and export. A reference bar
  uses its pattern's seed; a unique bar has its own.
- Placing a pattern puts one song bar per pattern bar. Bars that would pass the end of
  the song are not placed; playback and export see the same timeline. A reference to a
  pattern bar the pattern no longer has plays that index modulo the pattern's bar count.
- Unique bars: Make Unique, or any edit of the bar in the step grid. All 64 bars can be
  unique.
- Loop region (Song mode): the bars before the region play once, in order (absolute bar
  *B* < `loopStart` plays song bar *B*). From the bar where the host reaches the region,
  *B* plays `loopStart + (B − loopStart) mod loopLength`: the region is entered at its
  first bar and repeats in order, for any start and length. A seek uses the same mapping,
  so the position alone decides the bar. Bars after the region are not reached while the
  loop is on. The timeline playhead uses the same function (`kit::songBarAt`).
  Without a loop the song repeats after its last bar.
- Entire Song export: one pass from bar 1 to the last bar; the loop region is ignored.
  End of Track is at the end of bar 16, 32 or 64. A hit before position 0 (negative
  microtiming on the first step of bar 1) is not exported, as in a pattern export; in
  continuous playback that hit belongs to the end of the previous pass.
- Song or Pattern playback is project state (DSNG), not a host parameter. A project
  saved before this release opens in Pattern mode with the initial song; its patterns
  are untouched.
- Realtime: the message thread publishes under a spin lock held for a copy; the audio
  thread uses `tryEnter` only and copies what changed into storage it owns. It never
  touches a `shared_ptr`. Worst case after a project restore: 10 patterns (287 KB) and
  64 unique bars (459 KB) copied once.
- Bounds: a block visits at most 3 bars, and per bar the steps overlapping the block
  plus 4; a step yields at most 16 × 8 hits.

## 3. Mixer, sends and outputs (tap-point model)

```
pad voice -> level/pan (at trigger) -> mute/solo gate (3 ms ramp) -+-> ONE dry destination: Main, or Out 2..9
                                                                    +-> Send 1 (post-fader) -> delay  -> Main
                                                                    +-> Send 2 (post-fader) -> reverb -> Main
```

- **Host buses: Main + Out 2..Out 9 = 9 stereo buses, 18 channels.** Out 2..9 are
  disabled by default; the host enables them. Only stereo or disabled is accepted.
- The engine's ten auxiliary pairs are **internal**: 2 send pairs and 8 output pairs,
  rendered into buffers the processor owns (`scratch_`, sized in `prepareToPlay`).
  They are never host buses. `kit::hostOutputBuses == 9` is asserted at compile time.
- A pad routed to an output the host has disabled plays on Main (bit-identical to the
  enabled bus). The MIXER and ROUTING pages show this.
- A pad is never on Main and on its output at once. Returns go to Main only.
- A block larger than the prepared maximum is rendered in chunks of the internal
  buffers; no index leaves an allocated buffer.
- Solo: while any pad is soloed, the others are muted. Mute and solo stop new hits and
  fade running voices.
- Tails: the returns continue after the voices end and stop by themselves (delay
  feedback ≤ 85 %, delay ≤ 2 s). All Sound Off (CC120), panic and `reset()` clear them.
  `getTailLengthSeconds()` reports 30 s.
- Level, pan and sends are read when a hit starts (as before this release).

## 4. Compatibility decisions

1. **Host parameters are append-only.** The 119 parameters of DRM-03b/04 keep their IDs,
   order, ranges and meaning. 55 are appended: per pad `send1`, `send2`, `solo`; and
   `drm.fx.delay.{time,feedback,tone,level}`, `drm.fx.reverb.{size,damp,level}`. Total 174
   (`products/drums/parameters.csv`: 55 lines added, none changed). No `slotNN.pXXX` ID exists.
2. **DSEQ v3** adds the step lock (flag bit 2). v1 and v2 load unchanged.
3. **DPAD v2** appends 16 pad outputs and the voice limit. v1 loads as Main and 64 voices.
4. **New sections** DSNG v1, DGEN v1, DUIS v1. Missing: defaults. Malformed: the whole
   state is rejected and the live instrument is unchanged.
5. `drm.seq.pattern` choice names are unchanged (owner decision still open).
6. Send effects are a small dedicated delay and reverb (`SendFx`), not `EffectsChain`:
   its generic amount/rate/character parameters do not express a tempo-synced delay.
7. Macros on PERFORMANCE are eight knobs bound to existing host parameters (kit volume,
   velocity, swing, accent, delay feedback and return, reverb size and return).
8. The 1 MiB state limit holds: the largest tested state (64-bar song with unique bars)
   is checked in `testState`.

## 5. Defect found and fixed during this work

1. The 64-bar parity test showed playback diverging from the export from bar 5 on: the
   audio thread's song view resolved an A bar's pattern slot to the first slot. Root
   cause: GCC 13.3 generates wrong code for `(c ? a : b)[std::min<int> (x, y)]` with
   pointer operands (the false arm reads uninitialised temporaries; reproduced in a
   12-line file at `-O0`, clang compiles it correctly). The source was valid C++; the
   expression is now written with a named index and carries a comment. Builds with other
   compilers were not shown to have the defect. The parity test is the regression test.
2. Review 6795902654: the loop region was out of phase for a nonzero start
   (`loopStart + B mod length`). It now follows the mapping in section 2;
   `DrumsKitEngineTests.cpp` `testSongLoopRegion` covers starts and lengths that do not
   divide, seeks into and out of the region, the disabled loop and the engine's playback.

## 6. Limits of the automated evidence

- Automated tests do not establish DAW routing, sound quality or visual acceptance.
- Timing numbers measured in a shared container are not a performance claim; the
  Windows numbers are the ones attached to the PR.
- Factory sounds were not auditioned by a person.
