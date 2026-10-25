# Rack and instrument workflows

Product source: [PRODUCT_SPEC](../product/PRODUCT_SPEC.md).
This is a textual workflow specification, not a rendered/accepted screen.

## Global shell

- Header: Scene, Undo/Redo, A/B, host tempo, root/scale, Panic and output meters.
- Left rack: slot, full instrument name, MIDI channel/source, preset, mute/solo,
  activity, level and output.
- Workspace: Sound / Pattern / Modulation / Effects for the selected instrument.
- Generator panel: supported profiles, musical controls, separate Sound/Pattern locks,
  Generate Sound / Generate Pattern / Generate Both and variant history.
- Preset browser: Instrument/Style/Type filters.
- Bottom: keyboard for pitched instruments, pad bank for Drums.

Add Instrument groups by style then family.
Full-on Bass and Goa Bass are separate instrument identities, sharing DSP components.
No mandatory second Style selector is needed after selecting a style-specific instrument.
Several styles may coexist in the same rack.

## Sound

Bass: source, phase/retrigger, amp/filter/pitch envelopes, filter/drive and macros.
Lead/Keys: voices, synthesis, envelopes and unison where supported.
FX: event family, duration/target beat and evolution.
Drums: pads, synth/sample controls, gain/trim and choke group.
Capabilities decide which controls exist; absent features are not fake enabled buttons.

## Pattern

One underlying musical model, several views:
- Bass/Lead: notes and velocity, step view or piano roll.
- Keys: polyphonic piano roll and chord controls.
- Drums: independent pad lanes.
- FX: event timeline with duration/target.

Selected slot/instrument ownership is always visible.
Arp/Pattern is a selected instrument capability, not a separate instrument in the catalog.

## Generate, audition, keep

Generate Sound preserves notes; Generate Pattern preserves the sound.
Mutate respects locks and selected ranges.
Changing a profile prepares the next generation; it does not silently replace the playing sound.
The user can audition, compare, accept or undo a candidate.
UI and realtime state swap safely; an error leaves the last valid result intact.

## Export

Drag MIDI / Save MIDI uses the captured audible variant, not a fresh random generation.
After export, Host mode plays the DAW MIDI; Internal continues the internal sequence;
Both is deliberate. Avoid double playback.
Internal sound modulation is not presented as portable MIDI data.
FX has a planned WAV render/export workflow with documented tail handling.

## Evidence

Capture actual selected instruments and non-default states.
Check resize/scaling, numeric values, focus, disabled states, routing and automation.
Do not infer visual acceptance from existing UI PR #43 or a generic component showcase.
