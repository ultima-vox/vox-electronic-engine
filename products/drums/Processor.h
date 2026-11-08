#pragma once

#include "common/SuiteProcessor.h"

namespace vox::drums {

// FOUND-01 shell. The product engine, parameter map (parameters.csv) and
// editor are added by the product's *-01 PR.
class DrumsProcessor final : public vox::suite::SuiteProcessor {
public:
    DrumsProcessor();
};

} // namespace vox::drums
