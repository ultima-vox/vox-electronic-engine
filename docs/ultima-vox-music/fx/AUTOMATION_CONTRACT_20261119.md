# FX-06 automation — the 19 host lanes checked against the running plugin

**Draft / HOLD / NO MERGE.** New executable check only; no parameter ID, range, default or DSP behaviour changed. The frozen contract `products/fx/parameters.csv` is now enforced against the plugin rather than only documented.

## What is now executable

`tests/suite/FxAutomationTests.cpp` (CTest `suite_fx_automation_tests`, label `suite`, invoked with `products/fx/parameters.csv`) opens a real `FxProcessor` and asserts:

- the plugin exposes **exactly 19** host parameters, in the same order as the CSV;
- every parameter carries a stable string ID, and the 8 macro lanes plus the 4 slot wet/amount pairs are present by name;
- the host-normalised default equals the contract default for every lane;
- the `automatable` flag matches the contract for every lane;
- a host write inside a `beginChangeGesture` / `endChangeGesture` pair lands on the base value within the lane's own quantisation (the 1 ms duration grid, the 0/1 boolean grid);
- out-of-range host input stays finite and the ranged float lanes stay inside the contract range;
- gesture begin/end and value notifications are delivered once to listeners, and a write while the editor is open is mirrored into the editor knob without a feedback gesture;
- loading a different Program leaves the slot, macro and output automation bases untouched.

The harness runs on the same pinned-JUCE build as the processor test, so this exercises the plugin's real parameter objects, not a model of them.

## Why the ranges look the way they do

`FX 1 Wet` etc. are declared `0..1`, default `1`, and the processor keeps them as host bases that the audio thread clamps and smooths; the 8 macros are bipolar sources with default `0.5` (neutral). The duration lane is `0.05..32 s` and deliberately snaps to 1 ms, which the test tolerates explicitly instead of pretending a raw 0.37 write is bit-exact.

## Still open on FX-06

- Real DAW automation readback and touch/latch behaviour in Cubase 12–15 and REAPER (owner acceptance, not a Linux check).
- Native Tab/Escape/focus order, DPI 100–200 %, screen-reader labels; the Source panel now has an explicit focus scope, but no native Windows or DPI evidence exists yet.
- Transport seek/loop/discontinuity and 16-bar Transition in a real host (synthetic PlayHead cases exist).
- Screen-reader and tooltip coverage of every panel; only the offscreen editor path is covered today.

**Not qualified:** any of the above in Windows, and any musical/automation judgement in a host. No Windows workflow ran and none was requested.

## Verified at `553e98f`

**52/52 Linux executables** pass at `553e98f1b75c21a48828e37dcca96c41163fa08f`: [48 standalone](evidence/automation-inventory-20261119.txt) plus the [four pinned-JUCE native](evidence/automation-native-20261119.txt) harnesses, which now include `FxAutomationTests` (processor 0 failures, runtime stress 0 failures, export hashes unchanged, automation contract 0 failures).
