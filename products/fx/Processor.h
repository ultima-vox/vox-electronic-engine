#pragma once

#include "common/SuiteProcessor.h"

namespace vox::fx {

// FOUND-01 shell. The product engine, parameter map (parameters.csv) and
// editor are added by the product's *-01 PR.
class FxProcessor final : public vox::suite::SuiteProcessor {
public:
    FxProcessor();
};

} // namespace vox::fx
