#pragma once

#include "common/SuiteProcessor.h"

namespace vox::syn {

// FOUND-01 shell. The product engine, parameter map (parameters.csv) and
// editor are added by the product's *-01 PR.
class SynthesisProcessor final : public vox::suite::SuiteProcessor {
public:
    SynthesisProcessor();
};

} // namespace vox::syn
