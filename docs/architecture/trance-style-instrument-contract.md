# Style-specific rack instruments — implementation contract

Product decisions: one VST3 rack; separate Full-on Bass/Drums/Lead/Keys/FX
and later Goa/Dark instruments. This document defines responsibilities, not a new binary API.
Existing IDs/state/host automation remain protected.

## Identity and composition

| Concept | Identity/ownership |
| --- | --- |
| VST3 host | Existing plugin identity; generic routing/state/editor composition |
| Rack slot | Stable SlotId, independent of display index, MIDI channel and chosen instrument |
| Instrument | Stable InstrumentId, descriptor, own init/presets/generator grammar/capabilities |
| DSP family | Reusable internal components; not another rack or a host-visible style switch |
| Style/family labels | Catalog grouping metadata; not a substitute for InstrumentId |
| Content pack | Stable pack/content identity and explicit version/compatibility/resource references |

Full-on Bass and Goa Bass are distinct catalog instruments even when they share
oscillators, filters and envelopes. Neither should be implemented by copying a complete engine.
Several instances and several styles can coexist in one rack.

Do not repurpose or rename the existing Psy Bass InstrumentId for Full-on.
Allocate new stable identities during the scoped registration task; keep legacy restore explicit.
Changing display labels does not change identity.

## Provider boundary

Use the existing registry/descriptor/factory/instance composition.
A provider resolves the exact requested InstrumentId and builds its configured instrument.
Core routing, state, presets and shell do not branch on genre names.
A missing/incompatible module preserves identity and payload; it is not substituted with another style.

Keep the existing POD/C ABI v1 unchanged in this router task.
Style/family grouping may initially live in control-thread catalog metadata.
Adding a field to a persisted format or external ABI is a separate versioned change.
Future Pattern v2 needs an explicit adapter; do not reinterpret current 64-step bytes.

## Instrument-owned behaviour

Each instrument defines:
- actual parameter ranges/defaults, voice/event/resource bounds and supported controls;
- a usable Init sound;
- its own Sound, Pattern and Generator profile catalogs;
- constrained Sound generation and style-specific Pattern grammar;
- supported modulation and expression;
- state/content compatibility.

Sound generation changes the patch only.
Pattern generation changes the musical material only.
Both is an explicit operation respecting independent sound/pattern locks.
The saved project stores actual generated parameters and notes, not only a seed/preset name.
Seed, algorithm version and content version are distinct.

Sharing a DSP family does not automatically make presets cross-compatible.
A transfer between instrument identities must be explicit, validated and undoable.

## Timing, routing and automation

The host supplies tempo/position/root/scale and delegates instrument generation.
A selected instrument does not own host MIDI policy or route another slot's events.
Note ownership is by SlotId, including during channel/zone changes.
GUI audition addresses the selected slot directly.
Panic, overload, replacement and unresolved-state behaviour must be explicit.

Reuse the existing fixed host-facing slot macro bank.
Assignments persist; changing instrument does not renumber host parameter endpoints.
Do not dynamically rebuild the VST3 parameter topology for each style.
Generate is a discrete control-thread operation; restore reuses its committed result.

## UI workflow, without new drawings

- Add Instrument: style group -> instrument family -> full instrument name.
- Rack row: Full-on Bass, slot identity, channel/source, preset, mute/solo, activity, level/output.
- Selected editor: Sound / Pattern / Modulation / Effects.
- Sound: specialised real controls, shared presentation components.
- Pattern: instrument-owned step/piano-roll/pad/event editor as supported.
- Generator: separate Sound/Pattern/Both, profile controls, locks, audition, A/B and undo.
- Style is already chosen by loading the instrument; a second mandatory global style selector is unnecessary.
- Catalog filtering does not replace a playing instrument.
- Replacing a module is a deliberate action with state/voice safety; not a dropdown side effect.
- No decorative graphs or controls pretending to be connected to DSP.
- Existing VOX tokens/components are reused; accepted visual layout still requires actual editor evidence.
- No artwork/mockup is produced before an explicit request to draw.

## Delivery and test contract

First reliable pipeline: existing rack + Bass sound/generation/MIDI/restore;
a new Full-on identity/content is registered after the Bass baseline is understood.
AGY owns the Bass diagnostic packet; this work owns router safety and integration contracts.

For each style instrument verify identity resolution, channel isolation, multiple instances,
replacement while sounding, Sound/Pattern isolation, locked content, save/reopen,
missing content, automation and bounded processing.
A separate package/licence does not waive these requirements.
Runtime remains offline; licence/content operations do not enter the audio callback.
