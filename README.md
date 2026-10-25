# Vox Trance Engine

Windows x64 VST3 rack for offline sound and MIDI generation.
Target hosts: Cubase 12–15 and REAPER 7.x on Windows 10–11.

**Product direction is agreed; the production instruments are not yet delivered.**
The current binary still identifies itself as **Vox Electronic Engine**.
This documentation/archival change does not rename the installed plugin or change its IDs.

## Product

One rack hosts separate style-specific instruments:
Full-on Bass, Full-on Drums, Full-on Lead, Full-on Keys and Full-on FX.
Each owns its presets, generator rules and specialised controls; DSP components are shared.
Further styles can be added as instruments/content without duplicating the host.

Generation is offline, without neural networks.
Sound and Pattern generation remain separate.
Style instruments can be distributed individually or in bundles.
The first instrument to finish is **Full-on Bass**; a working empty rack alone is not the deliverable.

## Start here

- [Documentation index](docs/README.md)
- [Product specification](docs/product/PRODUCT_SPEC.md)
- [Current implementation status](docs/STATUS.md)
- [Execution roadmap](docs/ROADMAP.md)
- [Protected contracts](docs/architecture/CONTRACTS.md)
- [Build and validation](docs/BUILD.md)
- [UI design rules](docs/ui/DESIGN_SYSTEM.md)
- [UI workflows](docs/ui/WORKFLOWS.md)
- [AGY task: reproduce Bass/generation failures](docs/tasks/AGY_BASS_DIAGNOSTICS.md)

## Current source layout

| Path | Responsibility |
| --- | --- |
| `apps/vst3/` | JUCE host adapter and editor composition |
| `libs/instrument/`, `libs/rack/` | Providers, stable identities, routing and state |
| `libs/sequence/`, `libs/transport/`, `libs/midi/` | Current sequence model, host time, scheduling and MIDI export |
| `libs/bass/`, `libs/modules/` | Existing Psy Bass implementation and built-in provider composition |
| `libs/acid/`, `libs/lead/`, `libs/semanticfx/`, `libs/atmos/` | Existing modules still compiled and tested |
| `libs/parts/` | Existing compatibility/Part code; retain until references and migrations are audited |
| `libs/preset/`, `libs/modulation/`, `libs/effects/` | Presets, modulation and creative processing |
| `libs/ui/`, `libs/vox-ui/` | Product UI and shared presentation components |
| `tests/` | Existing regression, integration, performance and reliability suites |
| `docs/` | Current product, engineering, UI and task documentation |
| `archive/` | Historical documents and inactive source; not build inputs |

Do not remove a compiled module merely because it is absent from the first commercial package.
Catalog visibility, installed content and persistent InstrumentId are different concerns.

## Build

Use the [Windows build instructions](docs/BUILD.md).
Build/package filenames deliberately retain `Vox Electronic Engine` during this transition.
Compilation and automated tests do not establish sound quality or acceptance in a DAW.

## History

Old product specifications, the separate-Drums product policy and inactive Kick/MATCH
source are preserved in [the archive](archive/README.md).
They are historical evidence, not current product requirements.
