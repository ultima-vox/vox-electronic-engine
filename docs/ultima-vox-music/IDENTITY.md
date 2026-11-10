# ULTIMA VOX MUSIC — frozen plugin identities

**Status:** frozen at FOUND-01 by owner decision D2 (#112). Source of truth: `cmake/suite/SuiteIdentity.cmake`. Tests: `suite_identity_tests`, `suite_identity_doc`, and `suite_moduleinfo`, all under the CTest label `suite`.

None of these values may change after the first published build. A change would break every saved DAW project that uses the plugin.

| Product | `PRODUCT_NAME` | `BUNDLE_ID` | `PLUGIN_CODE` | VST3 component CID | State tag |
|---|---|---|---|---|---|
| Trance Synthesis | VOX Trance Synthesis | com.ultimavox.vox-trance-synthesis | VxTS | ABCDEF019182FAEB556C567856785453 | `VXTS` |
| Drums | VOX Drums | com.ultimavox.vox-drums | VxDr | ABCDEF019182FAEB556C567856784472 | `VXDR` |
| FX | VOX FX | com.ultimavox.vox-fx | VxFX | ABCDEF019182FAEB556C567856784658 | `VXFX` |

All three products share these fields:
- **Manufacturer code:** `UlVx` (`556C5678`).
- **Company:** `Ultima Vox`.
- **Format:** VST3 instrument with MIDI input, no MIDI output and no audio input.

## How the CID is derived

JUCE 9.0.1 builds the component ID with `juce::VST3Interface::jucePluginId` (`juce_VST3Interface.h`), from the manufacturer code and the plugin code. The CID string consists of four 32-bit words:

| Word | Value |
|---|---|
| 1 | `ABCDEF01` |
| 2 | `9182FAEB` |
| 3 | manufacturer code |
| 4 | plugin code |

`JUCE_VST3_COMPONENT_CLASS` and `JUCE_VST3_CAN_REPLACE_VST2` are not used.

The CID values were computed with that function; `suite_identity_tests` recomputes them. `suite_moduleinfo` reads the same CIDs from the `moduleinfo.json` that is built into each bundle.

## Never reused

These identities belong to the legacy Rack and must not appear in the suite:
- `com.ultimavox.vstengine` / `DkP1`;
- `com.ultimavox.fullon-drums`;
- `com.ultimavox.fullon-bass`;
- Rack parameter IDs `slotNN.pKKK`.

Old Rack projects do not open in these plugins: their state is rejected with a diagnostic.

## Parameter IDs

Each product freezes its own `parameters.csv` in its `*-01` PR. ID scheme: `<prod>.<group>.<name>` with prefixes `syn.`, `drm.` and `fx.` (FOUND-00 §5.2).
