# Rack MIDI hardening — verification report

Date: 25.10.2026.
Base main: b076a53d8900b53433e8aa4fdb1027c8f2bed135.
Scope: router safety, style-instrument responsibility contract and textual UI workflow.
AGY Bass/provider/processor implementation files are untouched.

## Results

| Check | Observed result |
| --- | --- |
| Original actual-source router tests | 27 checks PASS |
| Added next-block recovery regression before fix | FAIL at expected missing-recovery assertion |
| Final actual-source router tests | 137 checks PASS |
| Final router AddressSanitizer + UndefinedBehaviorSanitizer | 137 checks PASS |
| Instrument contract tests | 8172/8172 PASS |
| Modulation tests | 57/57 PASS |
| Independent Architecture Guard/realtime diff review | PASS, no remaining blocker/major |
| git diff --check | PASS |
| Windows Release/full CTest/package | NOT RUN locally; PR CI required |
| Cubase/REAPER, sound/visual/manual acceptance | NOT RUN; REQUIRED |

LeakSanitizer could not inspect restricted /proc process metadata.
ASAN_OPTIONS=detect_leaks=0 was used for the sanitized run;
address and undefined-behaviour checking remained active. No leak-check pass is claimed.

## Actual commands

Run from the repository root. Output binaries stay in ignored build/rack-audit.

```bash
mkdir -p build/rack-audit
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Ilibs libs/rack/RackRouter.cpp tests/rack/RackRouterTests.cpp -o build/rack-audit/rack_router_tests
build/rack-audit/rack_router_tests
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -Ilibs libs/rack/RackRouter.cpp tests/rack/RackRouterTests.cpp -o build/rack-audit/rack_router_tests_sanitized
ASAN_OPTIONS=detect_leaks=0 build/rack-audit/rack_router_tests_sanitized
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Ilibs libs/instrument/HostParameterSchema.cpp libs/instrument/InstrumentRegistry.cpp libs/instrument/ModuleState.cpp tests/instrument/InstrumentContractTests.cpp -o build/rack-audit/instrument_contract_tests
build/rack-audit/instrument_contract_tests
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Ilibs libs/modulation/Modulation.cpp libs/modulation/ModulationState.cpp tests/modulation/ModulationTests.cpp -o build/rack-audit/modulation_tests
build/rack-audit/modulation_tests
git diff --check
```

Local compiler: GCC 13.3.0, Linux.
These are real project sources, not mocks of the router implementation.
They do not constitute a full plugin build; CMake/JUCE/Windows host tooling was not available locally.

## Behavioural coverage

- External, GUI audition and per-slot generated ownership.
- Lost releases, lost CC120/123, synthetic retrigger releases and audition switching.
- Recovery follows stable SlotId through reorder/disable, not array position.
- Other slots retain their held-note ownership.
- Old NoteOff after recovery is ignored.
- Module replacement, disappearance and reset cancel obsolete pending recovery.
- Emergency events precede fresh same-offset notes.
- All sixteen layered slots may recover in one block.
- Original audition/generated channels are used for release.
- Malformed sizes/status/data cannot index outside note ownership arrays.
- Invalid IDs/routing modes/zones and oversize slot spans are rejected/bounded.

## Remaining rack-validation work

| Area | Current evidence | Remaining acceptance |
| --- | --- | --- |
| Router isolation/release safety | Actual router tests and sanitizer pass | Real DAW routing and audio behaviour |
| Load/replace instruments | Existing Rack code reviewed; reset/forget boundary preserved | Full rack integration suite/DAW replacement while sounding |
| Mute/solo | Existing bypass/reset path inspected; not changed | Actual sustained-note and automation behaviour in DAW |
| State/project restore | IDs/schemas unchanged; adjacent module contract suite passes | Full state suites and project reopen |
| Style instruments/UI | Responsibilities/workflow documented | Registration/content implementation and owner visual acceptance |
| Bass timbre/generation | No diagnosis claimed by this patch | AGY task #45 |

No statement here declares the rack production-ready.

## AgentReport

```yaml
agent_report:
  role: Lead / Rack integration
  task: recover dropped MIDI releases and protect router input boundaries
  result: PARTIAL
  files_changed:
    - libs/rack/RackRouter.h
    - libs/rack/RackRouter.cpp
    - libs/rack/RackTypes.h
    - tests/rack/RackRouterTests.cpp
    - docs/architecture/rack-midi-release-safety.md
    - docs/architecture/trance-style-instrument-contract.md
    - docs/testing/rack-midi-hardening-report.md
  contracts_touched:
    - internal MIDI note ownership and deterministic overload recovery
    - routing validation; no instrument ABI or persistent schema change
  assumptions:
    - Rack invokes route before audition/generated routing once per block
  implementation:
    - fixed SlotId recovery queue and next-block slot-wide All Sound Off
    - original MIDI channels retained for internal-source releases
    - validate channel-voice events before ownership lookups
  tests_added:
    - 110 additional behavioural checks, retaining the original 27
  verification:
    - command: standalone actual-source router / sanitized router / instrument / modulation tests
      result: PASS
    - command: Windows x64 Release / full CTest / package
      result: NOT_RUN
    - command: actual Cubase/REAPER acceptance
      result: NOT_RUN
  architecture_changes:
    - bounded runtime-only fail-safe; new style responsibilities documented
  migration_impact:
    - none; no serialized identifiers or formats changed
  performance_impact:
    - fixed 16-entry queue; at most 256 recovery events per rack block
    - no allocation/locks introduced by inspection; full processor stress suite not run locally
  security_impact:
    - malformed MIDI bytes cannot index note arrays
  manual_acceptance_required:
    - actual DAW routing, state, automation, note/tail and sound acceptance
  unresolved:
    - required Windows CI and full plugin/host validation
    - broader rack state/lifecycle validation and AGY Bass evidence
  risks:
    - emergency silence cuts all notes/tails of the overloaded slot
    - offline/DAW production acceptance is not inferred from standalone tests
```

Merge recommendation: WAIT for required Windows CI/full suite.
Manual gate: REQUIRED.
Next task: validate full rack lifecycle/state and actual DAW behaviour after the router fix.
