#include "PluginEditor.h"
#include "RackRoutingPanel.h"
#include "instrument/HostParameterSchema.h"
#include "modules/BuiltInProvider.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

// Protect the ordinary host/test stack footprint; moving the test instance to
// heap alone would hide an oversized production processor object.
static_assert(sizeof(VstEngineAudioProcessor) <= 256u * 1024u,
              "processor inline state exceeds the reviewed host stack budget");

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
// Direct-parameter fixture covering every ParameterType; pool indices 0..3.
constexpr const char *directFixtureId = "test.editor.direct";
class DirectFixtureInstance final
    : public vstengine::instrument::InstrumentInstance {
public:
  bool prepare(const vstengine::instrument::PrepareSpec &) override {
    return true;
  }
  void reset() noexcept override {}
  void suspend() noexcept override {}
  void resume() noexcept override {}
  void setBypassed(bool) noexcept override {}
  void process(const vstengine::instrument::ProcessBlock &) noexcept override {}
  bool setParameter(std::string_view id, float v) noexcept override {
    return std::isfinite(v) &&
           (id == "level" || id == "steps" || id == "sync" || id == "wave");
  }
  bool loadState(std::uint32_t,
                 std::span<const std::byte> bytes) noexcept override {
    return bytes.empty();
  }
  bool saveState(std::span<std::byte>, std::uint32_t &n) const noexcept override {
    n = 0;
    return true;
  }
  std::uint32_t latencySamples() const noexcept override { return 0; }
  std::uint32_t tailSamples() const noexcept override { return 0; }
};
class DirectFixtureProvider final
    : public vstengine::instrument::InstrumentProvider {
  vstengine::instrument::InstrumentDescriptor descriptor_;

public:
  DirectFixtureProvider() {
    using vstengine::instrument::ParameterType;
    auto &d = descriptor_;
    d.id = directFixtureId;
    d.providerId = "test.editor.provider";
    d.name = "Direct controls probe";
    d.vendor = "Tests";
    d.directParameters = true;
    d.budget.maxVoices = 1;
    d.budget.maxMidiEventsPerBlock = 2048;
    d.budget.maxStateBytes = 8;
    d.parameters = {
        {"level", "Level", "dB", 0, 10, 2, 0, ParameterType::floating,
         "Output", {}, true, true, -1, 0},
        {"steps", "Steps", "", 1, 16, 4, 1, ParameterType::integer,
         "Motion", {}, true, true, -1, 1},
        {"sync", "Sync", "", 0, 1, 0, 1, ParameterType::boolean,
         "Motion", {}, true, true, -1, 2},
        {"wave", "Wave", "", 0, 2, 0, 1, ParameterType::choice,
         "Osc", {"Saw", "Square", "Sine"}, true, true, -1, 3}};
  }
  std::span<const vstengine::instrument::InstrumentDescriptor>
  descriptors() const noexcept override {
    return {&descriptor_, 1};
  }
  std::unique_ptr<vstengine::instrument::InstrumentInstance>
  create(std::string_view id,
         const vstengine::instrument::CreateContext &) override {
    if (id != directFixtureId)
      return {};
    return std::make_unique<DirectFixtureInstance>();
  }
  std::span<const vstengine::instrument::ContentDescriptor>
  contentDescriptors(std::string_view) const noexcept override {
    return {};
  }
};

// The Macros page shows one real, host-bound control per direct parameter
// of the selected slot and nothing for an empty slot.
bool directInstrumentControlsWork() {
  using Page = vstengine::ui::MainNavigation::Page;
  constexpr std::size_t slot = 2;
  VstEngineAudioProcessor processor(std::make_unique<DirectFixtureProvider>());
  processor.prepareToPlay(48000, 64);
  juce::String diagnostic;
  if (!processor.loadSlotInstrument(slot, directFixtureId, diagnostic)) {
    std::cerr << "direct fixture load failed: " << diagnostic << '\n';
    return false;
  }
  std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
  auto *editor = dynamic_cast<VstEngineAudioProcessorEditor *>(base.get());
  if (editor == nullptr)
    return false;
  editor->setSize(1240, 800);
  editor->showPageForTesting(Page::macros);

  // Slot without an instrument: the section exists and has no controls.
  processor.selectSlot(slot + 1);
  editor->refreshForTesting();
  auto *empty = dynamic_cast<juce::Label *>(named(*editor, "rack.direct.empty"));
  if (named(*editor, "rack.direct.controls") == nullptr || empty == nullptr ||
      empty->getText() != "No instrument controls" ||
      named(*editor, juce::String(vstengine::instrument::hostparams::poolId(
                         slot + 1, 0))) != nullptr)
    return false;

  processor.selectSlot(slot);
  editor->refreshForTesting();
  const auto id = [](std::size_t pool) {
    return juce::String(vstengine::instrument::hostparams::poolId(slot, pool));
  };
  const auto raw = [&processor](const juce::String &key) {
    auto *value = processor.parameters().getRawParameterValue(key);
    return value != nullptr ? value->load() : -1.0e9f;
  };
  auto *level = dynamic_cast<juce::Slider *>(named(*editor, id(0)));
  auto *steps = dynamic_cast<juce::Slider *>(named(*editor, id(1)));
  auto *sync = dynamic_cast<juce::ToggleButton *>(named(*editor, id(2)));
  auto *wave = dynamic_cast<juce::ComboBox *>(named(*editor, id(3)));
  if (level == nullptr || steps == nullptr || sync == nullptr || wave == nullptr) {
    std::cerr << "direct controls missing\n";
    return false;
  }
  if (empty->isVisible() || wave->getNumItems() != 3 ||
      wave->getItemText(2) != "Sine" || level->getMaximum() != 10.0 ||
      steps->getMinimum() != 1.0 || steps->getMaximum() != 16.0)
    return false;
  // Controls are laid out inside the page, not collapsed.
  for (auto *control : std::array<juce::Component *, 4>{level, steps, sync, wave})
    if (control->getWidth() <= 0 || control->getHeight() <= 0)
      return false;

  level->setValue(7.5, juce::sendNotificationSync);
  steps->setValue(9.0, juce::sendNotificationSync);
  sync->setToggleState(true, juce::sendNotificationSync);
  wave->setSelectedId(3, juce::sendNotificationSync);
  if (std::abs(raw(id(0)) - 7.5f) > 1.0e-4f || raw(id(1)) != 9.0f ||
      raw(id(2)) != 1.0f || raw(id(3)) != 2.0f) {
    std::cerr << "direct controls did not reach host parameters\n";
    return false;
  }
  sync->setToggleState(false, juce::sendNotificationSync);
  wave->setSelectedId(2, juce::sendNotificationSync);
  if (raw(id(2)) != 0.0f || raw(id(3)) != 1.0f)
    return false;

  // Host-side changes are reflected by the bound controls.
  if (auto *parameter = processor.parameters().getParameter(id(1))) {
    parameter->setValueNotifyingHost(parameter->convertTo0to1(12.0f));
    if (steps->getValue() != 12.0)
      return false;
  } else {
    return false;
  }

  // Every supported editor size keeps the section inside the page.
  for (const auto size : {juce::Point<int>{1040, 680}, juce::Point<int>{1180, 760},
                          juce::Point<int>{1500, 920}}) {
    editor->setSize(size.x, size.y);
    auto *section = named(*editor, "rack.direct.controls");
    if (section == nullptr || section->getHeight() <= 0)
      return false;
    const auto page = editor->workspaceBoundsForTesting();
    const auto inEditor = editor->getLocalArea(section, section->getLocalBounds());
    if (!page.contains(inEditor))
      return false;
  }
  return true;
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

    // A fresh Rack is empty: the editor says so and offers nothing to tweak.
    editor->refreshForTesting();
    auto *title = dynamic_cast<juce::Label *>(named(*editor, "rack.instrument.title"));
    auto *subtitle = dynamic_cast<juce::Label *>(named(*editor, "rack.instrument.subtitle"));
    auto *macro = named(*editor, "rack.sound.macro.1");
    if (title == nullptr || title->getText() != "Empty slot" || subtitle == nullptr ||
        !subtitle->getText().contains("Choose an instrument") || macro == nullptr ||
        macro->isEnabled()) {
      std::cerr << "fresh Rack editor does not show the empty-slot state\n";
      return EXIT_FAILURE;
    }

    // The rest of this smoke test exercises Bass (slot 1) and Acid (slot 2),
    // loaded explicitly as a user would.
    juce::String fixtureDiagnostic;
    if (!processor.loadSlotInstrument(0, vstengine::modules::bassInstrumentId,
                                      fixtureDiagnostic) ||
        !processor.loadSlotInstrument(1, vstengine::modules::acidInstrumentId,
                                      fixtureDiagnostic))
      return EXIT_FAILURE;
    editor->refreshForTesting();
    if (!macro->isEnabled() || title->getText() == "Empty slot")
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

    // Built-in Bass has no direct parameters: no instrument controls.
    editor->showPageForTesting(vstengine::ui::MainNavigation::Page::macros);
    editor->refreshForTesting();
    auto *noDirect =
        dynamic_cast<juce::Label *>(named(*editor, "rack.direct.empty"));
    if (processor.instrumentRack().descriptor(processor.selectedSlotIndex()) ==
            nullptr ||
        processor.instrumentRack()
            .descriptor(processor.selectedSlotIndex())
            ->directParameters ||
        noDirect == nullptr || noDirect->getText() != "No instrument controls" ||
        named(*editor, juce::String(vstengine::instrument::hostparams::poolId(
                           processor.selectedSlotIndex(), 0))) != nullptr)
      return EXIT_FAILURE;
    if (!directInstrumentControlsWork())
      return EXIT_FAILURE;

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
    // Optional visual evidence: one capture per workspace page.
    const auto pageDirectory = juce::SystemStats::getEnvironmentVariable(
        "VOX_PAGE_SNAPSHOT_DIR", {});
    if (pageDirectory.isNotEmpty()) {
        editor->setSize(1240, 800);
        const std::array<std::pair<Page, const char *>, 6> pages{{
            {Page::sound, "sound"}, {Page::pattern, "pattern"},
            {Page::routing, "routing"}, {Page::zones, "zones"},
            {Page::macros, "macros"}, {Page::advanced, "advanced"}}};
        for (const auto &[page, name] : pages) {
            editor->showPageForTesting(page);
            if (!writeCapture(*editor, juce::File(pageDirectory)
                                           .getChildFile(juce::String(name) + ".png")
                                           .getFullPathName()))
                return EXIT_FAILURE;
        }
    }
    std::cout << "Editor smoke test passed\n";
    return EXIT_SUCCESS;
}
