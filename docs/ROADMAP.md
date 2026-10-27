# Execution roadmap

Updated:26.10.2026. Owner priority: **finish the production Rack; Bass afterwards**.
Another agent is developing Acid Synth and its dedicated UI in parallel.
Small engineering checkpoints provide evidence; they are not permission to ship
incomplete controls or label an MVP production.

| Order | Work | Exit evidence / tracker |
| --- | --- | --- |
| 0 | Audit current main and correct unsupported diagnoses | Source-level capability audit complete; direct main release probe27 PASS; exact old owner session still unconfirmed |
| 1 | Freeze the generic Rack control/audio/state contracts | Reviewed named parameters, bus/source identities, state authority/rollback and full resource bounds; #53/#56 |
| 2 | Implement Rack audio graph and Send FX/returns | Actual sends/returns, tap/mute/solo/tail/latency policy, state and routing tests; #54 |
| 3 | Implement explicit internal kick ducking | Documented event-trigger/audio-detector source contract, target gain envelope, routing/state and block/timing evidence; #55 |
| 4 | Finish shared Rack UI, automation and integration | Real MIDI/audio/ducking controls, host/UI/state agreement, correct slot mappings and native captures; #57, related #28/#5 |
| 5 | Production Rack milestone | Windows x64 VST3/full checks, RT/performance evidence and integrated Cubase/REAPER/visual acceptance; #53/#4 |
| Parallel | Acid Synth and Acid-specific UI, externally owned | Real descriptor/DSP/presets/generation/panel bindings using the agreed host adapter; no parallel shared schema rewrite |
| 6 | Complete Full-on Bass after Rack | Every accepted VOX control/bank, usable sound/pattern generation, factory content, MIDI, state and musical/host acceptance; #58/#45 |
| 7 | Specify/review and implement musical Pattern v2 | Polyphonic/long-phrase event model, version/adapter/resource policy and scheduler/export tests; existing Lead8/Atmos6 DSP voices do not complete this model |
| 8 | Add Full-on Drums | Kit/pad/choke/resources, patterns and actual kick source integration with the established Rack bus |
| 9 | Finish Full-on Lead / Keys / FX | Full instrument controls/content; motifs/arp/chords/voice leading, synth keys and semantic FX/WAV workflow as applicable |
| 10 | Package instruments/content | Offline installation, identity/version policy and selected licensing |
| 11 | Final production acceptance | Complete lineup, installer/upgrade, real editor, sound review and measured Windows/DAW workload matrix |

## Owner change — independently delivered instruments (27 October 2026)

Instrument DSP/UI are installed separately from the one Rack VST3. The Rack
owner must review the [module design packet](architecture/INDEPENDENT_INSTRUMENTS.md)
and integrate a loader, discovery, lifecycle, state and a viable DAW automation
strategy. Acid implements the first independent module against the reviewed
boundary. Existing static-provider candidates do not satisfy this new gate.
No project or production acceptance is implied by this documentation update.

## Current task and boundaries

[RACK_PRODUCTION](tasks/RACK_PRODUCTION.md) is the active lead packet.
[Main audit](testing/rack-capability-audit-20261026.md) separates present code from
missing features and unconfirmed causes. The existing Main-only mixer is useful
infrastructure; Send FX, internal ducking and complete user routing are not present.

Acid DSP and its dedicated UI belong to the other agent. Shared Rack/provider
host/state contracts belong to one lead; publish/review their integration adapter
before requiring the Acid panel to bind unpublished experiments. Do not edit
Acid-specific implementation as part of the shared Rack task.

Bass WIP is retained but deferred. Its reduced13/16-control Sound composition is
not the accepted product surface. Preserve [full Bass acceptance](product/FULLON_BASS_ACCEPTANCE.md)
and do not add legacy POC compatibility modes: the owner confirms no old projects
require migration. Version intentional replacement/rejection; preserve plugin
identity and realtime/routing safety.

## Integrated milestone policy

The Rack milestone must exercise actual host MIDI -> audio, internal patterns,
Generate/Mutate publication, export/re-import, project reopen, panic/ownership,
returns/ducking, tails, latency and lifecycle under declared workloads. Deterministic
test instruments can verify infrastructure before every production instrument exists.

Manual Cubase/REAPER checks occur at integrated milestones, not after each PR.
Automated green checks do not prove DAW routing, visual acceptance, factory timbre
or production readiness. Standalone is not required.

## Historical work

PRs #46/#48/#50 integrate routing/lifecycle/live-control repairs. PR #51 integrates
verified scheduler/export/diagnostic corrections from AGY's original packet.
Remaining exact-session/musical Bass acceptance stays open in #45 after Rack.
The earlier AGY diagnosis packet is not the current parallel work assignment.

## Open decisions

- Target CPU/RAM/interface/buffer and measured supported workload budgets.
- Reviewed Rack bus counts, tap policies, kick source contract and effect latency.
- Exact hidden synth-page/performance semantics and full instrument resource limits.
- Whether piano must be realistic acoustic rather than synthetic.
- Commercial distribution/licence/device-transfer policy and factory quantities.
