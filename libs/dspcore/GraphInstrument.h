#pragma once
// Graph instruments in the Rack (#75 phase 2): a compiled patch exposed
// through the same InstrumentProvider / InstrumentInstance contract as built-in
// and native instruments, so routing, the host pool, state and the editor are
// unchanged. No DLL is involved.
#include "dspcore/PatchGraph.h"
#include "instrument/InstrumentContract.h"

#include <memory>
#include <string>

namespace vstengine::dspcore {

// Controller thread. Parses graph.json text (schema in docs/architecture/
// DSP_PATCH_GRAPH.md) into a spec. Returns false with a reason.
[[nodiscard]] bool parsePatchJson(const std::string &text, PatchSpec &out, std::string &why);

// Controller thread. Parses and compiles; nullptr with a reason on failure.
[[nodiscard]] std::shared_ptr<const CompiledPatch> loadPatch(const std::string &json,
                                                             std::string &why);

[[nodiscard]] std::unique_ptr<instrument::InstrumentProvider>
makeGraphProvider(std::shared_ptr<const CompiledPatch> patch);

} // namespace vstengine::dspcore
