# DSP patch graph — slice 1 (PROPOSED, #75)

Status: proposed, not frozen. Owner plan: issue #75. Scope: one instrument's
voice. This is **not** the Rack audio bus / FX graph.

## Model

A graph instrument is data (`graph.json`). The controller thread parses and
validates it, then compiles it into an immutable `CompiledPatch` with a fixed
evaluation order. The audio thread only steps that plan: no parsing,
allocation, locks or file access. The patch is exposed through the existing
`InstrumentProvider` / `InstrumentInstance` contract (`providerId`
`vox.graph`). Routing, the host pool, automation, state and the editor are
therefore unchanged. No DLL is involved.

Code: `libs/dspcore/Primitives.h` (DSP Core), `libs/dspcore/PatchGraph.*`
(spec, compiler, voice), `libs/dspcore/GraphInstrument.*` (JSON, provider).

## graph.json v1

| Field | Meaning |
|---|---|
| `graphVersion` | `1`. Any other value is rejected. |
| `instrument` | `id` (durable InstrumentId), `name`, `vendor`, `version`. |
| `voice` | `policy` `mono-last` (the only policy in slice 1); `ampEnvelope` = id of the env node that ends the voice. |
| `parameters[]` | `id`, `name`, `type` (`float`/`int`/`bool`/`choice`), `min`, `max`, `default`, `pool` (stable host pool index), optional `unit`, `group`, `choices`, `smoothing` (seconds, one-pole). |
| `nodes[]` | `id`, `type` (`block@version`), `params` (block param → parameter id), `inputs` (block input → `node.port`). |
| `output` | `node.port` of type audio. |

The parameter `id` and `pool` index are durable identity, exactly as for
native instruments: never renumber them.

## Blocks (slice 1)

| Block | Inputs | Output | Params |
|---|---|---|---|
| `env.adsr@1` | — | control | attack, decay, sustain, release (s) |
| `osc.blep@1` | — (pitch from the voice) | audio | wave (0 sine, 1 saw, 2 square) |
| `filter.svf.lp@1` | in: audio | audio | cutoff (Hz), resonance (0..1) |
| `amp.vca@1` | in: audio, env: control | audio | — (`in * env * velocity`) |
| `amp.gain@1` | in: audio | audio | gain |

A block version is a behaviour contract: a changed algorithm is a new
version (`@2`); `@1` keeps rendering the same samples.

## Validation (compile errors carry a reason)

Unknown graph version, voice policy, block or block version; duplicate
node/parameter id or pool index; an invalid parameter (id, range,
default, smoothing over 1 s); an unknown
parameter reference; a missing, dangling or wrongly typed input (control into
audio); a cycle; a non-audio output; a missing or non-envelope `ampEnvelope`;
more than 64 nodes; JSON over 256 KiB.

## Voice semantics (slice 1)

Mono, last-note priority with a held-key stack; sustain pedal (CC64), CC120
(sound off, immediate), CC123 (notes off, normal release); velocity scales
linearly. Per sample: smoothed parameters, then envelopes; if the amplitude
envelope is idle the voice outputs 0 and resets filter state; otherwise the
remaining nodes run in compiled order. Identical to the native Vox Demo Synth,
which is the parity oracle (`graph_demo_parity_tests`, bit for bit).

## Delivery

Slice 1 ships the graph Demo as a **factory graph** compiled into the plugin
(`VOX_FACTORY_GRAPHS` in CMake → `generated/FactoryGraphs.h`). Installable
graph packages arrive with the `.vins` ZIP loader (#73/#75); the same
`loadPatch` is used there. Out of scope here: polyphony, modulation matrix,
FX blocks, sample playback, a graph editor UI.
