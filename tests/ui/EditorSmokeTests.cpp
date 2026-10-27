#include "PluginEditor.h"
#include "RackRoutingPanel.h"
#include "modules/BuiltInProvider.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
juce::Component *named(juce::Component &parent, const juce::String &id) {
  for (auto *child : parent.getChildren()) {
    if (child->getName() == id)
      return child;
    if (auto *result = named(*child, id))
      return result;
  }
  return nullptr;
}
bool writeCapture(juce::Component &component, const juce::String &path) {
  if (path.isEmpty())
    return true;
  const auto image =
      component.createComponentSnapshot(component.getLocalBounds());
  juce::FileOutputStream output{juce::File(path)};
  juce::PNGImageFormat png;
  if (!output.openedOk())
    return false;
  output.setPosition(0);
  output.truncate();
  return png.writeImageToStream(image, output);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    VstEngineAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
    auto* editor = dynamic_cast<VstEngineAudioProcessorEditor*>(base.get());
    if (editor == nullptr)
        return EXIT_FAILURE;

    // The open editor must follow project restores without publishing its old
    // local model over the newly restored pattern.
    processor.prepareToPlay(48000, 64);
    auto sceneEdit = processor.sequenceEditSnapshot();
    sceneEdit.value.setLength(32);
    if (!processor.commitSequenceEdit(sceneEdit))
      return EXIT_FAILURE;
    juce::MemoryBlock scene;
    processor.getStateInformation(scene);
    sceneEdit.value.setLength(16);
    if (!processor.commitSequenceEdit(sceneEdit))
      return EXIT_FAILURE;
    editor->refreshForTesting();
    if (editor->patternLengthForTesting() != 16)
      return EXIT_FAILURE;
    processor.setStateInformation(scene.getData(),
                                  static_cast<int>(scene.getSize()));
    editor->refreshForTesting();
    if (editor->patternLengthForTesting() != 32)
      return EXIT_FAILURE;
    if (processor.commitSequenceEdit(sceneEdit))
      return EXIT_FAILURE;

    // Module identity restored under an open editor refreshes the actual
    // selector and catalogs as well as the local pattern.
    juce::MemoryBlock bassScene;
    processor.getStateInformation(bassScene);
    juce::String loadDiagnostic;
    if (!processor.loadSlotInstrument(0, vstengine::modules::acidInstrumentId,
                                      loadDiagnostic))
      return EXIT_FAILURE;
    editor->refreshForTesting();
    auto *instrument = dynamic_cast<juce::ComboBox *>(
        named(*editor, "rack.instrument.selector"));
    const auto acidName =
        juce::String(processor.instrumentRack().descriptor(0)->name);
    if (!instrument || instrument->getText() != acidName)
      return EXIT_FAILURE;
    processor.setStateInformation(bassScene.getData(),
                                  static_cast<int>(bassScene.getSize()));
    editor->refreshForTesting();
    const auto bassName =
        juce::String(processor.instrumentRack().descriptor(0)->name);
    if (instrument->getText() != bassName || bassName == acidName)
      return EXIT_FAILURE;

    const auto selection = processor.selectedSlotIndex();
    auto *power = dynamic_cast<juce::Button *>(named(*editor, "slot01Enabled"));
    auto *emptyPower =
        dynamic_cast<juce::Button *>(named(*editor, "slot16Enabled"));
    if (!power || !emptyPower || emptyPower->isEnabled())
      return EXIT_FAILURE;
    power->setToggleState(false, juce::sendNotificationSync);
    if (processor.parameters().getRawParameterValue("slot01Enabled")->load() !=
            0 ||
        processor.selectedSlotIndex() != selection)
      return EXIT_FAILURE;
    power->setToggleState(true, juce::sendNotificationSync);
    if (processor.parameters().getRawParameterValue("slot01Enabled")->load() !=
        1)
      return EXIT_FAILURE;
    RackRoutingPanel routing(processor);
    auto *send =
        dynamic_cast<juce::Slider *>(named(routing, "slot01.send.1.level"));
    auto *type =
        dynamic_cast<juce::ComboBox *>(named(routing, "rack.return.1.type"));
    auto *source =
        dynamic_cast<juce::ComboBox *>(named(routing, "rack.duck.source"));
    if (!send || !type || !source)
      return EXIT_FAILURE;
    send->setValue(0.4, juce::sendNotificationSync);
    type->setSelectedId(7, juce::sendNotificationSync);
    source->setSelectedId(2, juce::sendNotificationSync);
    if (std::abs(processor.parameters()
                     .getRawParameterValue("slot01.send.1.level")
                     ->load() -
                 0.4f) > 1e-6f ||
        processor.parameters()
                .getRawParameterValue("slot02.send.1.level")
                ->load() != 0 ||
        processor.parameters()
                .getRawParameterValue("rack.return.1.type")
                ->load() != 6 ||
        processor.parameters()
                .getRawParameterValue("rack.duck.source")
                ->load() != 1)
      return EXIT_FAILURE;
    if (!writeCapture(routing, juce::SystemStats::getEnvironmentVariable(
                                   "VOX_ROUTING_SNAPSHOT", {})))
      return EXIT_FAILURE;

    const auto duckCapturePath =
        juce::SystemStats::getEnvironmentVariable("VOX_DUCKING_SNAPSHOT", {});
    if (duckCapturePath.isNotEmpty()) {
      juce::Viewport *view = nullptr;
      for (auto *child : routing.getChildren())
        if (auto *viewport = dynamic_cast<juce::Viewport *>(child))
          view = viewport;
      if (view == nullptr)
        return EXIT_FAILURE;
      view->setViewPosition(0, 180);
      if (!writeCapture(routing, duckCapturePath))
        return EXIT_FAILURE;
      view->setViewPosition(0, 0);
    }

    using Page = vstengine::ui::MainNavigation::Page;
    for (const auto page : { Page::sound, Page::pattern, Page::routing,
                             Page::zones, Page::macros, Page::advanced }) {
        editor->showPageForTesting(page);
        if (editor->currentPageForTesting() != page)
            return EXIT_FAILURE;
    }

    for (const auto size : { juce::Point<int> { 1040, 680 },
                             juce::Point<int> { 1180, 760 },
                             juce::Point<int> { 1500, 920 } }) {
        editor->setSize(size.x, size.y);
        const auto coveredWidth = editor->keyboardKeyWidthForTesting(Page::rack)
                                  * 43.0f;
        const auto componentWidth = static_cast<float>(
            editor->keyboardComponentWidthForTesting(Page::rack));
        if (std::abs(coveredWidth - componentWidth) > 0.5f)
            return EXIT_FAILURE;
        if (!editor->headerControlsFitForTesting())
          return EXIT_FAILURE;
        const auto rack = editor->rackBoundsForTesting();
        const auto workspace = editor->workspaceBoundsForTesting();
        const auto keyboard = editor->keyboardBoundsForTesting();
        if (rack.isEmpty() || workspace.isEmpty() || keyboard.isEmpty()
            || rack.intersects(workspace) || rack.intersects(keyboard)
            || workspace.intersects(keyboard))
            return EXIT_FAILURE;
    }
    const auto snapshotPath = juce::SystemStats::getEnvironmentVariable(
        "VOX_UI_SNAPSHOT", {});
    if (snapshotPath.isNotEmpty()) {
        editor->setSize(1240, 800);
        editor->showPageForTesting(Page::sound);
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
        juce::FileOutputStream output { juce::File(snapshotPath) };
        juce::PNGImageFormat format;
        if (!output.openedOk())
            return EXIT_FAILURE;
        output.setPosition(0);
        output.truncate();
        if (!format.writeImageToStream(image, output))
            return EXIT_FAILURE;
    }
    std::cout << "Editor smoke test passed\n";
    return EXIT_SUCCESS;
}
