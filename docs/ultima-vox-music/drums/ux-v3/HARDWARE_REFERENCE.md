# VOX Drums — hardware-inspired UI visual reference (pre-release)

## Scope

Open **[hardware-reference.html](hardware-reference.html)** locally in a browser; it is a **self-contained interactive VISUAL REFERENCE**, **not** the shipping plugin, an HTML runtime, a JUCE implementation or an approved source of functional controls. Click **Workspace** to cycle Beatmaking → Sound Design → Mixing. Pad Rack toggles Grid / List; Mixer switches Collapsed / Compact / Expanded. All values, note grids, waveform and meters are **illustrative dummy state**. Native implementation must bind the actual processor.

This reference translates the user-provided *physical hardware instrument* direction into concrete reusable style cues: machined dark metal chassis, rubber playable pad faces, metallic rotary dials and vertical fader caps, inset meter wells, restrained illuminated arcs, separate measurable audio metering, deliberate shadows, no excessive photorealistic textures. It is **not a copy** of Akai Maschine, MPC, Groove Agent or another commercial product. The product is a unique VOX Drums software instrument.

### Approved functional authority

- [UI/UX v3 issue #127](https://github.com/ultima-vox/vox-electronic-engine/issues/127): functional layout/interaction is authoritative.
- [v3.2 conceptual screens](../ux-v3/screens/01-beatmaking.png) in the branch this documentation extends are prior *architecture reference*, not the final physical finish.
- [Generator #128](https://github.com/ultima-vox/vox-electronic-engine/issues/128): actual genre generation and semantic roles in #132/#133.
- [DUIS/DLAY contract PR #130](https://github.com/ultima-vox/vox-electronic-engine/pull/130): layout/state persistence needs release-policy review before implementation.

### Non-negotiable visual implementation contract

1. **No default JUCE skin.** Use native JUCE interaction/automation primitives with custom \`vox-ui\` skin and reusable JUCE painting: textured *forms* (not external bitmap wallpaper), subtle material gradient, edge light, recessed grooves, hardware motion states, keyboard focus, high-DPI scaling and clear accessible hit rectangles. Existing eight-page editor and older \`VoxKnob\` colors/geometry are donors for behavior only. No HTML/WebView in final VST3.
2. **Tactile Pad Rack:** exactly sixteen equal nearly square tiles in a 4×4; a large rubber **audition** surface and a separate understandable **select** affordance; consistent name/MIDI note/Mute/Solo/selection/missing sample treatment, List presentation in same panel.
3. **Real rotary controls** for Sound Tune/Decay/Filter/Drive/Level, Pan and FX Sends. Larger primary dials around 52 logical px when space permits, secondary ~34 px, visible value and units, bipolar center on Pan. Control bodies must have clear pointers and calibrated ranges. Proper vertical faders for levels; don't render everything as flat horizontal sliders.
4. **One dockable hybrid workspace:** limited side docking; 1–2 simultaneous central editors; pinned context Inspector and Quick Controls, a genuine signal-flow Sound Editor and distinct Sample editor; one single bottom Mixer with Collapsed / Compact / Expanded; lane parameter edits stay within the Step Sequencer; make density manageable at 1040×700.
5. **Truthful state:** Kit Level affects **all outputs**, Main L/R meter measures Main pre-host; an \`OVER ≥0 dBFS\` latch is **not** verified audible clipping; no fake CLIP badge. Parameter color arc ≠ measured audio meter ≠ MIDI velocity flash. Gate 2 needs truthful RT-safe telemetry or an honest unavailable state.
6. **Do not infer API functionality from illustration:** this HTML uses some shorthand labels as illustrations. Verify any actual control with production code and approved #127 mapping. Any unsupported action must be removed or explicitly scoped; do not create false host Play/Stop, MIDI insert-to-track, unsupported kit/player features.
7. **Performance/quality:** custom controls reusable for future VOX Synth/FX, no audio-thread painting / allocations / blocking, bounded UI invalidations; validate Windows 125/150/200% and Cubase/REAPER. JUCE real-editor screenshots at actual 1440×900 and 1040×700 before broad UI rollout.

## Change control

Owner requested an **original software interpretation of a physical drum instrument**, not a cloned interface. Reference styling is a **design direction**, not a license to remove approved v3.2 behavior or default to stock JUCE visual components. Native UI remains subject to owner visual sign-off and independent gate, and it must not regress processor DSP, routing, MIDI export, factory content or host automation. This document and HTML are visual assets only; they do not authorize a merge.
