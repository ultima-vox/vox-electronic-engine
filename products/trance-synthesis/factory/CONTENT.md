# Factory content ledger

The installed tables are original candidates for VOX Trance Synthesis. No third-party preset bank, sample or MIDI file was copied.

| Count | Value |
|---|---|
| Installed programs | 600 |
| Candidate programs | 600 |
| Human-approved programs | 0 |
| Installed phrases | 120 |
| Human-approved phrases | 0 |

Primary roles follow the owner quota: Bass 120, Acid 90, Lead 110, Pluck/Keys 80, Pad/Atmos 90, Arp 75, Stab 35. Transition risers and impacts are not synth programs.

`ProgramCategory` ordinals stored in user bank UBNK are unchanged. Factory role is a separate field.

`COVERAGE.csv` is the role × style × engine matrix, including N/A cells. `midi/FP-*.mid` is SMF type 0 at 960 PPQ and 120 bpm. The file stores tick, note, velocity and duration. Slide and accent are not in the SMF; the text event says so. Native audition applies them: Acid and Psy glide across a slide, VA does not; accent does not rewrite velocity. A render CSV is written when `VOX_FACTORY_AUDIT` points at a file. Human approval is not implied by that file.

`tests/suite/fixtures/ubnk-b0f230f.voxstate` is a UBNK v1 blob in the layout introduced by `b0f230f`. Category ordinals in it are the pre-role-split values.

