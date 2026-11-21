#pragma once

#include "DrumsEditor.h"
#include "Processor.h"

namespace vox::drums::testutil {

// The unpublished eight-page editor remains a test reference for the
// unchanged donor controls/PatternEditor/SongStrip and their regression
// history. It is not the product's createEditor path. Native qualification
// independently exercises the actual production editor and all 174 bindings.
// Caller owns the returned editor, as with AudioProcessor::createEditor().
inline juce::AudioProcessorEditor* makeDonorEditor (DrumsProcessor& processor)
{
    auto editor = std::make_unique<suite::SuiteEditor> (processor);
    editor->setContent (std::make_unique<DrumsPanel> (processor));
    return editor.release();
}
} // namespace vox::drums::testutil
