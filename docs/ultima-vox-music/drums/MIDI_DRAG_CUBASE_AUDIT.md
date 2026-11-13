# VOX Drums: MIDI drag into Cubase — static audit and Windows test procedure

Status: **static code audit plus test procedure; no fix, no code change, no DAW run.** Task
order: issue #127 comment 6863921664, priority 2. Report from Gate B: dragging the MIDI button
into Cubase did not create a part; only the save dialog appeared.

Baseline: `drm/production-candidate` @ `c7c29bbbd6101dfb5e0cb2418a071d11caeb15b2` (PR #125),
JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`.

Evidence labels used below:

- **[code]** — read in the source at the commits above.
- **[test]** — covered by an existing automated test.
- **[unverified]** — needs the Windows/Cubase run in §4. Nothing here is claimed as fixed.

## 1. The code path

| Step | What happens | Where | Evidence |
|---|---|---|---|
| 1 | One button, "MIDI", does both jobs. Its tooltip reads "Click: save the pattern as a MIDI file. Drag: drop it into your DAW." | `PatternEditor.cpp:216` | [code] |
| 2 | **Click** (`onClick`, on mouse-up): freezes a snapshot and opens an async `FileChooser` (save dialog) in Documents. | `PatternEditor.cpp:217–232` | [code] |
| 3 | **Drag**: `PatternEditor` listens to the button's mouse events (`midi_->addMouseListener (this, false)`). In `mouseDrag`, once the pointer has moved more than 6 px it sets `dragging_ = true`, calls `midi_->setState (buttonNormal)`, then `startDrag()`. | `PatternEditor.cpp:233, 690–705` | [code] |
| 4 | `startDrag` → `exportForDrag` writes a new file, then calls `performExternalDragDropOfFiles ({ path }, canMove = false, this)` on the parent `DragAndDropContainer` (`DrumsEditor`). The return value is ignored and no completion callback is given. | `PatternEditor.cpp:673–688`, `DrumsEditor.h:44` | [code] |
| 5 | **File**: `%TEMP%\VOX Drums MIDI drags\<instance id>\<ms>-<n>-<uuid12>\VOX Drums Pattern <N>.mid` (or `VOX Drums Song.mid` when the scope is Entire Song). It is written with `replaceWithData`; a failure is shown as "MIDI export failed: …". | `PatternEditor.cpp:575–579, 605–617, 652–671` | [code] |
| 6 | **File lifetime**: the newest drag of the instance is never deleted. Older ones are deleted after 10 minutes, at the next drag. Other instances' folders are deleted after 1 day. So a file cannot disappear while a DAW imports it. | `PatternEditor.h:84–85`, `PatternEditor.cpp:619–645` | [code] |
| 7 | **Content**: the drag file and the saved file are byte-identical, and the export equals playback within 1 tick. | `DrumsCandidateTests.cpp` `testExport`, `testSongParity` | [test] |
| 8 | **JUCE on Windows**: `performExternalDragDropOfFiles` builds a `CF_HDROP` and queues a job on a one-thread `ThreadPool`. The job calls `OleInitialize`, then `DoDragDrop` with `DROPEFFECT_COPY`, **on that worker thread**, and returns at once. The drop source ends the drag when `QueryContinueDrag` reports no mouse button down. | JUCE `juce_DragAndDrop_windows.cpp:45–54, 263–337` | [code] |
| 9 | **JUCE `Button`**: `mouseDrag` calls `updateState (isMouseSourceOver (e), true)`, which sets the button back to *down* on every drag event while the pointer is over it. `mouseUp` fires the click when the button was down and over. | JUCE `juce_Button.cpp:484–511` | [code] |

No automated test drives the mouse gesture. The tests call `exportForDrag()` directly, so steps
3, 4 and 8 have never run in a test. No test drops a file into a host.

## 2. Hypotheses for "only the save dialog, no part in Cubase"

| ID | Hypothesis | Basis | Distinguishing test (§4) |
|---|---|---|---|
| H1 | **The click fires after a drag.** Step 3 sets the button to *normal* once. On the next drag event that is still over the 62 × 30 px button, step 9 sets it to *down* again. A mouse-up over the button then runs `onClick` and opens the save dialog, although a drag started. The same happens if the drag ends early (H3) and the user releases near the button. | [code] steps 3, 9; `dragging_` is not checked in `onClick` | T5 |
| H2 | **A click instead of a drag.** The button saves on click by design. If the pointer moved ≤ 6 px, or the user expected a click to export to the track, only the dialog appears. This matches the first wording of the report ("offers to save to the computer"). | [code] steps 1–3 | T1, T2 |
| H3 | **The OLE drag ends at once in this host.** `DoDragDrop` runs on a worker thread, not the thread that owns the mouse input. If OLE sees no button down on that thread, `QueryContinueDrag` drops immediately over the plugin's own window and nothing reaches Cubase. Whether this happens in Cubase or REAPER is **unknown**. | [code] step 8; [unverified] | T3 vs T4 (Explorer vs Cubase) |
| H4 | **Cubase does not accept this drop target or data** (e.g. the plugin window type, or the drop position). The data is a plain `CF_HDROP` file copy, the same as dragging from Explorer. | [unverified] | T4 succeeds but T6/T7 fail |
| H5 | **The file is missing or empty when Cubase reads it.** Not supported by the code: steps 5–7. | [code], [test] | T8 |

H1 and H2 are code-grounded and can produce exactly "dialog, no part". H3 and H4 need a run. More
than one can be true at once.

## 3. Candidate fixes (not implemented; only after §4 evidence)

- **For H1/H2 (small, deterministic):**
  - `onClick` returns without opening the dialog when a drag started in this gesture
    (`dragging_`, reset on mouse-down).
  - In UI v3 the drag handle and "Save MIDI file…" are separate controls (issue #127 design).
  - A regression test sends the real event order: `mouseDown` → `mouseDrag` → `mouseUp` over
    the button, and checks that no dialog opens and one drag file is written.
- **For H3:** a Windows-specific drag started on the message thread (or a JUCE change). This
  needs the evidence from T3/T4 first, and an Architecture Guard note because it touches
  platform code.
- **For H4:** depends on what Cubase reports. No guess.

Per the review, a reproducible defect on #125 is a release-gate concern. The fix goes to the
branch the owner names, with a regression test. The DAW result is confirmed again in Cubase and
REAPER before it is called fixed.

## 4. Windows test procedure (owner or tester)

**Setup**

- Windows 10/11 x64. Record the version and the display scale (100 % and 150 %).
- Cubase: record the exact version and build (Help › About). REAPER: record the version.
- VOX Drums VST3 from the PR #125 CI artifact. Record the run ID and the SHA-256 of
  `VOX Drums.vst3` (as printed in the CI log).
- Project at 145 BPM, 4/4. One **instrument track** with VOX Drums, plus one empty **MIDI
  track**.
- Screen recording on. Note where the pointer is released in each step.

**Steps** (Pattern scope, PATTERN/SONG page)

| T | Action | Expected by the code | Record |
|---|---|---|---|
| T1 | Click the MIDI button without moving the mouse | Save dialog opens (by design) | dialog yes/no |
| T2 | Press, move **less than 6 px**, release | Save dialog (no drag starts) | dialog yes/no |
| T3 | Press on MIDI, move > 20 px, leave the plugin window, release on the **Windows desktop / an Explorer folder** | A file `VOX Drums Pattern N.mid` appears there; no dialog | file yes/no; cursor during the drag (copy "+" / forbidden / normal) |
| T4 | Same, release on the **MIDI track** lane in Cubase's project window at bar 1 | A MIDI part with the pattern's notes; no dialog | part yes/no; any Cubase dialog or message |
| T5 | Start a drag (> 20 px) but release **back over the MIDI button** | With today's code: save dialog (confirms H1) | dialog yes/no |
| T6 | Release on the **instrument track** lane | As T4 | part yes/no |
| T7 | Release on the empty area below the tracks | Cubase may create a track and part | result |
| T8 | After T3/T4, open `%TEMP%\VOX Drums MIDI drags\` | Newest folder holds a non-empty `.mid` (size > 100 bytes) | path, size |
| T9 | Repeat T3–T7 in **REAPER** | File or item created | result |
| T10 | Repeat T4 at 150 % display scale, and with the plugin window "always on top" off/on | As T4 | result |
| T11 | Song scope: switch the scope to Entire Song, drag to a track | 16/32/64-bar part | part length |

**Reading the results**

- T1/T2 dialog, T3 file, T4 part → drag works; the report was a click (H2). Remedy: UI
  separation (#127).
- T5 dialog → H1 confirmed (a code bug) → small fix and test.
- T3 no file (and the cursor never shows "copy") → H3 (OLE drag ends early in this host).
- T3 file but T4/T6 no part → H4 (Cubase side). Record the Cubase version and the drop target;
  search Steinberg's documentation for MIDI file drop.
- T8 empty or missing file → H5 (not expected from the code).

## 5. Status

- Not reproduced. No Cubase run happened in this work, and the defect is **not fixed**.
- A code-grounded defect (H1) exists: the save dialog can open after a drag has started. It is
  reported here and has not been changed.
- The PR #125 Gate B checklist item E ("drag drops a clip on a MIDI/instrument track") stays
  open until §4 is run.
