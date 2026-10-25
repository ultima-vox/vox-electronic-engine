# Vox Trance Engine — project context

## Product and source of truth

Windows 10–11 x64 VST3 rack; Cubase 12–15 and REAPER 7.x.
C++20, JUCE 9.0.1, CMake.
Offline generation; no neural network.
See [product](../docs/product/PRODUCT_SPEC.md), [status](../docs/STATUS.md),
[contracts](../docs/architecture/CONTRACTS.md) and [roadmap](../docs/ROADMAP.md).

Full-on Bass/Drums/Lead/Keys/FX are independent style-specific rack instruments.
They reuse common DSP families; they are not separate VST3 hosts.
The first instrument to finish is Full-on Bass.
Current shipping/build name remains Vox Electronic Engine until a scoped migration.

## Product boundaries

- Drums now belongs inside Trance Engine as a style-specific rack instrument.
- Mastering/MATCH analysis remains outside this product's current scope.
- Pattern/Arp is an instrument capability; it is not a standalone rack identity.
- Existing Acid/Atmos/etc and Parts/legacy IDs remain until audited migration/removal.
- Archived Kick is source material, not an implemented/accepted Full-on Drums.
- Product scope changed by owner decision; stable contracts are still protected.

## Compatibility and realtime

Follow [protected contracts](../docs/architecture/CONTRACTS.md).
Stable identities, transactional state, explicit routing, note ownership and
bounded realtime processing are mandatory.
Pattern v2 is planned, not implemented by a documentation change.
No renumbering or repurposing old IDs for new style instruments.

## UI

[Design system](../docs/ui/DESIGN_SYSTEM.md) and [workflows](../docs/ui/WORKFLOWS.md).
Implementation: `libs/vox-ui/` shared primitives; `libs/ui/` product composition.
Visual Gate A: real editor evidence and explicit owner acceptance.
Functional Gate B: actual bindings, automation, state and host checks.
No drawing until requested.

## Delivery and limits

[Build](../docs/BUILD.md) documents real commands and the retained artifact name.
All required Windows checks must pass before merge.
Real DAW acceptance and listening must be reported separately from CTest.
CPU/RAM promises await agreed hardware; current test ceilings are regression limits.
Current AGY scope: [Bass diagnostics](../docs/tasks/AGY_BASS_DIAGNOSTICS.md).
