#include "SoundPage.h"

#include "AcidSoundWorkspace.h"
#include "GenericSoundWorkspace.h"
#include "PsyBassSoundWorkspace.h"
#include "modules/BuiltInProvider.h"
#include "acid/AcidProvider.h"

namespace vstengine::ui {

namespace {

constexpr int pagePadding = 8;
constexpr int heroGap = 7;

} // namespace

SoundPage::SoundPage()
{
    addAndMakeVisible (hero);
    hero.setIdentity (HeroIdentity::generic, instrumentName);
}

void SoundPage::selectWorkspaceFor (const instrument::InstrumentDescriptor* descriptor)
{
    // Releasing the previous workspace also releases its APVTS attachments.
    workspace.reset();

    // Compared against the provider-owned InstrumentId constants rather than
    // copied literals, so the dispatch cannot drift from the contract.
    if (descriptor != nullptr && descriptor->id == modules::bassInstrumentId)
        workspace = std::make_unique<PsyBassSoundWorkspace>();
    else if (descriptor != nullptr && descriptor->id == acid::instrumentId)
        workspace = std::make_unique<AcidSoundWorkspace>();
    else
        workspace = std::make_unique<GenericSoundWorkspace>();

    addAndMakeVisible (*workspace);
}

void SoundPage::bind (juce::AudioProcessorValueTreeState& state, const std::size_t slotIndex,
                      const instrument::InstrumentDescriptor* descriptor)
{
    instrumentName = descriptor != nullptr ? juce::String (descriptor->name)
                                           : juce::String ("Empty slot");

    // Recreate the workspace so that no control can retain a control reference
    // or attachment belonging to the previously selected slot.
    selectWorkspaceFor (descriptor);

    hero.setIdentity (workspace != nullptr ? workspace->getHeroIdentity()
                                           : HeroIdentity::generic,
                      instrumentName);
    if (workspace != nullptr)
        hero.setAccent (workspace->getIdentityAccent());

    if (workspace != nullptr) {
        workspace->bind (SoundWorkspaceBinding { state, slotIndex, descriptor });
        resized();
    }
}

juce::StringList SoundPage::getGateAVisualOnlyLabels() const
{
    return workspace != nullptr ? workspace->getGateAVisualOnlyLabels() : juce::StringList();
}

void SoundPage::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, metrics::corner);
    g.setColour (colours::border);
    g.drawRoundedRectangle (bounds, metrics::corner, 1.0f);
}

void SoundPage::resized()
{
    auto area = getLocalBounds().reduced (pagePadding);

    // The hero stays shallow; the workspace below it is the priority.
    const auto heroHeight = juce::jlimit (74, 116,
                                          static_cast<int> (area.getHeight() * 0.18f));
    hero.setBounds (area.removeFromTop (juce::jmin (heroHeight, area.getHeight())));
    area.removeFromTop (heroGap);

    if (workspace != nullptr)
        workspace->setBounds (area);
}

} // namespace vstengine::ui