#pragma once
#include "DrumsPages.h"

namespace vox::drums {
// A single Generator dock, with settings and explicit role-authoring views.
std::unique_ptr<Page> makeGeneratorDock (DrumsProcessor&);
} // namespace vox::drums
