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

`COVERAGE.csv` is the role × style × engine matrix, including N/A cells. `midi/FP-*.mid` is SMF type 0 at 960 PPQ. A render CSV is written when `VOX_FACTORY_AUDIT` points at a file. Human approval is not implied by that file.
