# Vox Electronic Engine — Agent Project Context

## Product

- Name: Vox Electronic Engine
- Purpose: modular/generative electronic instrument workstation hosted as one VST3 with an internal multi-slot rack.
- Primary reference host: Cubase on Windows x64.
- Primary stack: C++20, JUCE 9, CMake, VST3.

## Source of truth

- Product/master architecture: issue #10 and issue #11.
- UI canon: `docs/UI_DESIGN_SYSTEM.md`.
- Component catalog: `docs/UI_COMPONENT_CATALOG.md`.
- Visual/layout canon: `docs/UI_VISUAL_REFERENCE_SPEC.md`.
- Production UI implementation standard: PR #31 / resulting canonical document once merged.

Agents must fetch the current versions before making exact status claims.

## Architecture invariants

1. One plugin/kernel hosts a generic rack of up to 16 slots.
2. Core/Rack/Router/State/Preset/Processor do not depend on concrete Bass/Acid/Lead implementations.
3. Instrument resolution flows through registry/provider/descriptor/factory/instance boundaries.
4. Stable SlotId is independent of slot index, InstrumentId, MIDI channel and display order.
5. MIDI channel identity is separate from audio output identity.
6. Duplicate channel semantics are explicit SWAP/MOVE/LAYER, never silent accidental layering.
7. Note ownership is by SlotId so NoteOff returns to the slot that accepted NoteOn.
8. Runtime and persistent state remain separated.
9. Pattern and Sound presets remain independent; Full Rack/Scene stores both.
10. Missing/incompatible modules preserve identity/routing/zones/opaque state and do not silently fall back.
11. State load is transactional: parse -> validate -> resolve/construct -> prepare -> commit.
12. Audio thread permits no blocking I/O/locks/unbounded allocation.
13. Host automation and event timing preserve sample accuracy and stable parameter identity.
14. Existing plugin/VST3 identity is preserved unless an explicit migration plan is approved.
15. Architecture is frozen after the accepted rack/provider checkpoint; speculative core redesign is prohibited.

## Product boundaries

- Drums are not an Electronic Engine instrument; future Vox Drum Engine is separate.
- Kick/Bass mastering/matching belongs to Vox Mastering Engine.
- Arp/Sequence is a capability/workflow of a selected Part, not a standalone rack identity.

## UI acceptance

The design system is a contract, not inspiration.

Two gates:

1. **Visual Gate A** — screenshots/visual comparison must receive explicit owner approval.
2. **Functional Gate B** — all production controls bind to real DSP/state/automation and pass host/regression tests.

Stock/generic JUCE appearance is not production acceptance. Compilation and UI smoke tests do not imply visual approval.

## Runtime/reliability gates

- Preserve zero-allocation warmed `processBlock` guarantee.
- Preserve current performance/reliability stress suites.
- Preserve deterministic routing/state/generation behaviour.
- Preserve repeated prepare/reset/reprepare safety.

## Manual acceptance

Cubase is required for claims about:

- host routing behaviour;
- project save/reopen;
- automation recording/replay;
- actual generated/held-note behaviour;
- owner visual acceptance.

Agents must never infer these from CTest alone.

## Delivery

- Windows x64 VST3 must build in Release.
- Required CI must be green before merge recommendation.
- Deliver integrated installable plugin, not disconnected demo libraries.
