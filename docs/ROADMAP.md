# Execution roadmap

The target is production quality. Small engineering checkpoints provide evidence and rollback;
they are not permission to ship incomplete controls or call an MVP production.

| Order | Work | Exit evidence |
| --- | --- | --- |
| 0 | Reproduce current Bass/generation failures | Exact commit/state, dry renders, event trace, reproduced or explicitly unconfirmed cause |
| 1 | Validate existing Rack + one Bass path | MIDI isolation, note ownership, state restore, no empty-rack-only handoff |
| 2 | Specify/review musical Pattern v2 | Polyphony/long phrases, explicit v1 adapter, resource bounds and tests before contract implementation |
| 3 | Finish Full-on Bass | Independent Sound/Pattern generation, usable presets, MIDI export, automation and DAW restore |
| 4 | Add Full-on Drums | Kit state, pad lanes, choke groups, resources and explicit kick/Bass musical context |
| 5 | Add Full-on Lead | Motifs/arp, sound controls, state, exports and acceptance |
| 6 | Add Full-on Keys | Chords/voice leading, overlap, bells/plucks/pads/synth piano/e-piano |
| 7 | Add Full-on FX | Semantic events, target beat, long transitions, MIDI triggers and WAV tails |
| 8 | Package instruments/content | Offline installation, identity/version policy, compatibility and chosen licensing |
| 9 | Production acceptance | Actual editor, factory sound review, workloads, installer and Windows/DAW matrix |

Bass diagnosis can proceed against the existing v1 model while Pattern v2 is designed.
Do not make the first regression/render harness wait for speculative core work.

## First integrated musical handoff

An installable x64 VST3 containing the existing validated rack plus a working Bass path:
MIDI input -> sound, internal pattern -> sound, Generate -> changed pattern,
export -> re-import, project save -> reopen.
Include dry WAV/MIDI evidence and instructions for Cubase and REAPER.

The complete target lineup remains Bass, Drums, Lead, Keys and FX.
A separately released Full-on Bass must meet its complete applicable production gate;
it is not a substitute for the rest of the roadmap.

## Parallel work

AGY owns the [Bass diagnosis packet](tasks/AGY_BASS_DIAGNOSTICS.md).
Documentation/archival stays on the documentation branch.
A second worker must not redefine provider, state, routing or Pattern contracts in parallel.
Further implementation packets are assigned only after evidence from the first Bass pass.

## Unresolved decisions

- Valuable existing projects/presets requiring migration.
- Target CPU/RAM/interface/buffer and measured workload budgets.
- Whether piano must be realistic acoustic rather than synthetic.
- Commercial distribution, licence and device-transfer policy.
- Final accepted UI layout and factory-content quantities.
