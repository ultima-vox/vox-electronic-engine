# Independently installed Rack instruments — design and acceptance packet

Owner decision, 27 October 2026: **instrument DSP and instrument UI are not
embedded in the Rack VST3**. Users install and update each compatible instrument
separately; installing an instrument must not require rebuilding or replacing the
host plugin. The single VST3 remains the DAW-facing Rack with up to16 slots.
This supersedes the previous 1.0 static-provider/no-loader decision.

Status: **product requirement and proposed architecture, not implemented or
approved ABI**. Rack / PR #62 belongs to the Rack owner; Acid / PR #59 belongs to
the Acid owner. Shared contracts have one owner and require Architecture Guard
review. Do not silently reclassify current static providers as independent.

## Package and runtime boundary

For the Windows10/11 x64 target, an owned instrument package contains a native
module (a DLL or equivalent loadable binary), a versioned manifest, the resources
needed for its instrument workspace, and its Sound / Pattern / Generator content.
The Rack is one VST3; these instrument modules are not additional DAW VST3 plugins
and this does not imply hosting arbitrary third-party VSTs. License and purchase
policy remain undecided; runtime discovery and use remain offline.

Each package declares stable `InstrumentId`, `providerId`, `packId`, package/instrument/state
versions, compatible host and ABI ranges, binary architecture, resources and
inventory. Package dependencies and DSP-family reuse must have explicit versions:
shared DSP may be compiled into individual modules or shipped as separately
versioned shared dependencies, provided updates do not silently alter another
instrument or break its dependencies. Exact package format, installation paths,
authenticity policy and update UX require review; names above are requirements,
not a claim that a manifest schema exists.

Use a small versioned C ABI across the binary boundary, with explicit sizes,
capabilities, ownership and bounded caller-owned data. No C++ STL, JUCE object,
allocator ownership or exceptions may cross the boundary. Existing
`libs/instrument/InstrumentAbi.h` is a starting point, **not proof of a complete
or stable loader ABI**: entry points, descriptor/content lifetime, UI, compatibility,
errors, state and parameter discovery require an independent review. The current
`hostServices` is an unversioned `void*`; the ABI exposes no editor lifecycle or
versioned host gesture/resource API, and its parameter descriptor lacks some
native choice/direct/modulation metadata. Any lossless extension or intentional
versioned replacement needs review; do not mutate the V1 layout silently.

Specialized UI must travel with the instrument. A preferred design is a
versioned declarative description and resources rendered by host-owned widgets;
a reviewed binary UI interface may be considered if needed. An instrument's UI
must not depend on passing C++/JUCE component pointers across independently built
binaries. Its controls bind stable semantic parameters through a host-mediated
adapter; audio DSP never accesses UI state. A native-editor C interface, if chosen,
needs opaque handles, explicit HWND parenting, DPI/resize, host parameter gesture
callbacks and deterministic teardown. Validate Acid's full accepted reference
against the actual editor rather than assuming a generic parameter list is enough.

## Discovery, lifecycle and real-time

- Discover/validate package and binary compatibility off the audio thread. Reject
  duplicate instrument identities, ambiguous parameter IDs and host-ID collisions
  explicitly; never silently choose one of conflicting packages.
- Create/prepare instances, load resources and allocate fixed processing storage
  off the audio thread. In callbacks: no allocation, blocking locks, file/network
  access, binary loading/unloading or construction. Enforce declared resource and
  event budgets, and preserve MIDI NoteOff ownership and Panic.
- Pin a binary and its referenced data as long as any DSP instance, callback,
  descriptor or editor uses it. Deep-copy validated metadata where appropriate;
  wrappers retain a module lease, and the module destroys its own instances.
  Quiesce audio, editor timers and pending callbacks before unload. Keeping a
  loaded binary until processor/application teardown is a candidate initial policy,
  not a claim that safe hot unloading exists. Do not update a file underneath a
  live instance. Versioned staged
  installation and explicit activation/rollback must be reviewed for Windows file
  locking. An in-process DLL does not isolate a crash from the DAW.
- On missing, invalid or incompatible module, preserve InstrumentId, version,
  named values, patterns and opaque payload in an explicit unresolved slot; no
  fallback instrument or silent data loss. Reinstallation and compatible upgrade
  must restore the original sound and pattern when the module can read them.

## Host automation and project portability — unresolved gate

The set of VST3 host parameters is presented during plugin setup; existing hosts
may cache that set. A module installed later or missing when a project is opened
may therefore not have a host-visible endpoint at the necessary time. A project
state is ordinarily restored after instance construction. Merely generating a
hash from `InstrumentId` and parameter ID does not resolve enumeration, host
caching, duplicate IDs, or automation for a missing module. Steinberg's
[parameter contract](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Parameters%2BAutomation/Index.html)
requires a unique host parameter ID; its [communication FAQ](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Communication.html)
describes `kParamTitlesChanged` for metadata changes, not parameter-count/ID changes.

Before freezing the ABI or connecting Acid, the Rack owner must design and review
an approach that demonstrates all of the following in Cubase12–15 and REAPER:

1. A new module can be installed without replacing Rack and its parameters become
   available with a documented scan/restart/instance lifecycle.
2. Parameter identities and units remain stable across project save/reopen,
   host restart, module updates, additional installed modules and slot selection.
3. A project opened without its module retains module state and automation safely;
   reinstalling the module and reopening recovers them. No automation is silently
   reassigned to a different parameter or a different replacement instrument.
4. Literal and VST3 host-ID collisions reject deterministically; max slots,
   catalog/parameter/resource bounds and exhaustion behavior are explicit.
5. Sound, Pattern and Generator remain separate, and modulation alters effective
   values without overwriting another base parameter or host automation.

Candidate directions include a stable parameter catalog independent of binary
installation, or a permanently bounded generic bank with a reviewed, persistent
binding policy. Both require a plan for project portability and prevention of
cross-instrument automation reassignment. Neither is selected by this packet.

The existing PR #62 scheme enumerates a compiled catalog during construction.
Whether it can satisfy dynamic installation and missing-module portability is
**not established**. Do not claim compatibility until the reviewed design and real
host tests prove it. Old POC projects have no required migration, but existing
plugin identity and stable IDs remain protected unless explicitly reviewed.

## Acid integration and exit criteria

Acid should become an independently built package with its DSP, descriptor,
generator, Sound/Pattern/Generator presets and accepted Acid workspace. Remove
any reliance on an Acid-specific static DSP provider or duplicated parameter
bridge in the released Rack once the common adapter is ready. Do not delete
working Acid code merely to create an empty package or edit the Rack agent's
implementation while the shared interface remains unreviewed.

Minimum measurable integration gate:

- Build/install Rack and Acid separately on Windows; record binary hashes. Install,
  update and remove Acid without changing the Rack binary/hash or plugin identity.
- Discover, select and render two independent Acid slots; verify controls, packaged specialized
  editor, per-slot automation, presets, generation, playback, MIDI export and Panic.
- Open a saved project with the module missing, preserve state and identity,
  reinstall and recover it. Exercise incompatible versions, conflicting IDs,
  corrupted manifests and failed preparation with rollback.
- Verify load/unload/update while audio is active under declared lifecycle rules,
  no callback allocations/locks/I/O, correct declared latency and tail reporting, stress and
  target-hardware workload. A module DLL failure must be reported accurately;
  in-process crash isolation is not claimed.
- Pass Windows Release/full CTest/package, independent ABI/realtime review,
  and actual integrated Cubase/REAPER and owner visual/audio acceptance.

Open decisions: concrete ABI entry point and UI schema; host automation discovery
strategy and missing-module portability; package trust/signing/licensing; exact
resource budgets; host scan/restart behavior; shared DSP distribution policy.
None is solved solely by adding a `LoadLibrary` call.
