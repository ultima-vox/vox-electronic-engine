# VOX FX release evidence ledger

Baseline: `a23b67b02264ca42f2b9bf5f9fed8e233a31fe35` (TZ); implementation parent `f4e407fc379cbc36bd447af24e0626dc1c80fec8`. This is a gap ledger, not a release approval. Any later commit invalidates exact-HEAD qualification until rerun. Exact baseline Windows [suite 44367745359](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/44367745359) and [build 44367745537](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/44367745537) queued; no bundle SHA or validator result. Prior four Windows SEGFAULTs remain unverified. Linux standalone evidence is historical and partial; see `STATUS.md` and `DSP_BENCHMARK.md`. No manual DAW or listening approval has been performed.

| Test | Status / partial evidence at baseline | Exact-HEAD Windows / audio / manual |
|---|---|---|
| FX-T01 | PARTIAL: twelve preliminary family tests/reference; character approval missing | queued / no approved audio / not done |
| FX-T02 | PARTIAL: four layers and snapshots; independent pitch/filter missing | queued / not approved / not done |
| FX-T03 | OPEN: gain/pan only, not 20 typed curves or migration | queued / not approved / not done |
| FX-T04 | OPEN: seconds one-shot only, no complete modes | queued / not approved / not done |
| FX-T05 | PARTIAL: eight voices, four fades and drop counter standalone; held note ownership missing | queued / not approved / not done |
| FX-T06 | PARTIAL: standalone bars clock, transport tracker; no integrated 16-bar timing | queued / not approved / not done |
| FX-T07 | OPEN: key/scale musical proof absent | queued / not approved / not done |
| FX-T08 | PARTIAL: eight macros, limited routes/sources, standalone tests | queued / not approved / not done |
| FX-T09 | PARTIAL: effects tests; 4-drive saturation and tails require qualification | queued / not approved / not done |
| FX-T10 | PARTIAL: immutable leases, WAV/hash, concurrency and allocation tests; actual host flow unexecuted | queued / not approved / not done |
| FX-T11 | PARTIAL: deterministic generator and session A/B; full Program/curation unavailable | queued / not approved / not done |
| FX-T12 | OPEN: no approved catalog audit | queued / none / not done |
| FX-T13 | PARTIAL: standalone pattern and MIDI tests; host/DAW parity missing | queued / not approved / not done |
| FX-T14 | PARTIAL: offline WAV, MIDI and file worker tests; host file dialogs/transfer missing | queued / not approved / not done |
| FX-T15 | OPEN: editor only syntax checked here; DPI/runtime unexecuted | queued / N/A / not done |
| FX-T16 | PARTIAL: codec tests; full processor and project reopen unexecuted | queued / N/A / not done |
| FX-T17 | BLOCKED: historical Windows crashes; no current binary or validator evidence | queued / N/A / not done |
| FX-T18 | PARTIAL: Linux standalone allocation/stress and diagnostic timings only; Windows deadline matrix missing | queued / not approved / not done |
| FX-T19 | OPEN: complete asset/license inventory absent | queued / N/A / not done |
| FX-T20 | NOT READY: single integrated owner acceptance after candidate qualification | queued / not approved / not done |

## Content waves

| Wave | Target approved unique Programs | Approved / audited | Evidence |
|---|---:|---:|---|
| A | 120 | 0 documented | no catalog/listening sign-off |
| B | 300 cumulative | 0 documented | no catalog/listening sign-off |
| C | 600 cumulative | 0 documented | no catalog/listening sign-off |

48 deterministic procedural seeds and 12 patterns are prototypes, not approved factory content. Counts mean no documented approvals, not a claim that all seeds are unusable. Production 1.0 requires all 600 unless the owner changes the spec.
