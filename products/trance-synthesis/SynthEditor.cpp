#include "SynthEditor.h"

#include "FactoryBank.h"
#include "Parameters.h"
#include "Processor.h"

namespace vox::syn {

SynthPanel::SynthPanel (SynthesisProcessor& processor)
    : processor_ (processor),
      capacity_ ({}, "factory"),
      load_ ("Load", vox::ui::VoxButton::Type::Primary),
      save_ ("Save", vox::ui::VoxButton::Type::Secondary)
{
    capacity_.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (capacity_);
    search_.setTextToShowWhenEmpty ("Search programs", juce::Colours::grey);
    search_.onTextChange = [this] { refresh(); };
    addAndMakeVisible (search_);
    category_.addItem ("All", 1);
    for (std::size_t i = 0; i < categoryNames.size(); ++i)
        category_.addItem (categoryNames[i], static_cast<int> (i) + 2);
    category_.setSelectedId (1, juce::dontSendNotification);
    category_.onChange = [this] { refresh(); };
    addAndMakeVisible (category_);
    programs_.setModel (this);
    programs_.setRowHeight (22);
    addAndMakeVisible (programs_);
    name_.setTextToShowWhenEmpty ("Program name", juce::Colours::grey);
    addAndMakeVisible (name_);
    load_.onClick = [this] { loadSelected(); };
    save_.onClick = [this] { saveNamed(); };
    addAndMakeVisible (load_);
    addAndMakeVisible (save_);

    auto& state = processor_.parameters();
    juce::String lastGroup;
    for (const auto& entry : parameterCatalog)
    {
        const juce::String id (entry.id);
        const juce::String group = id.startsWith ("syn.a.") ? "Part A"
                                 : id.startsWith ("syn.b.") ? "Part B"
                                 : id.startsWith ("syn.fx.") ? "FX"
                                                             : "Output";
        if (group != lastGroup)
        {
            auto label = std::make_unique<juce::Label>();
            label->setText (group, juce::dontSendNotification);
            label->setJustificationType (juce::Justification::centredLeft);
            page_.addAndMakeVisible (*label);
            controls_.push_back (std::move (label));
            lastGroup = group;
        }
        if (entry.choice)
        {
            auto box = std::make_unique<vox::ui::VoxComboBox>();
            box->addItemList ({ "Psy Bass", "Acid", "VA" }, 1);
            page_.addAndMakeVisible (*box);
            combos_.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, entry.id, *box));
            controls_.push_back (std::move (box));
        }
        else
        {
            auto knob = std::make_unique<vox::ui::VoxKnob> (entry.name, vox::ui::VoxKnob::Size::Small);
            knob->setDoubleClickResetValue (entry.initial);
            page_.addAndMakeVisible (*knob);
            sliders_.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                state, entry.id, knob->getSlider()));
            controls_.push_back (std::move (knob));
        }
    }
    viewport_.setViewedComponent (&page_, false);
    viewport_.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport_);
    refresh();
}

void SynthPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::transparentBlack);
}

int SynthPanel::getNumRows()
{
    return static_cast<int> (visible_.size());
}

void SynthPanel::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool selected)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int> (visible_.size()))
        return;
    g.fillAll (selected ? juce::Colour (0xff1d3a4d) : juce::Colours::transparentBlack);
    g.setColour (juce::Colours::white);
    const auto row = visible_[static_cast<std::size_t> (rowNumber)];
    const auto category = categoryNames[static_cast<std::size_t> (
        row.factory ? processor_.factoryProgramCategory (row.index) : processor_.userProgramCategory (row.index))];
    const auto name = row.factory ? processor_.factoryProgramName (row.index) : processor_.userProgramName (row.index);
    g.drawText (name + "  ·  " + category, 6, 0, width - 8, height, juce::Justification::centredLeft);
}

void SynthPanel::listBoxItemDoubleClicked (int, const juce::MouseEvent&)
{
    loadSelected();
}

void SynthPanel::refresh()
{
    visible_.clear();
    const auto query = search_.getText().trim().toLowerCase();
    const int selected = category_.getSelectedId();
    const int wanted = selected <= 1 ? -1 : selected - 2;
    for (int i = 0; i < processor_.factoryProgramCount(); ++i)
    {
        if (wanted >= 0 && processor_.factoryProgramCategory (i) != wanted)
            continue;
        if (query.isNotEmpty() && ! processor_.factoryProgramSearch (i).toLowerCase().contains (query))
            continue;
        visible_.push_back ({ true, i });
    }
    for (int i = 0; i < processor_.userProgramCount(); ++i)
    {
        if (wanted >= 0 && processor_.userProgramCategory (i) != wanted)
            continue;
        if (query.isNotEmpty() && ! processor_.userProgramName (i).toLowerCase().contains (query))
            continue;
        visible_.push_back ({ false, i });
    }
    capacity_.setText ("Factory ready " + juce::String (factoryProgramsReady) + " / " + juce::String (factoryProgramCapacity)
                           + "    Phrases ready " + juce::String (factoryPhrasesReady) + " / " + juce::String (factoryPhraseCapacity)
                           + "    User " + juce::String (processor_.userProgramCount()),
                       juce::dontSendNotification);
    programs_.updateContent();
    programs_.repaint();
}

void SynthPanel::loadSelected()
{
    const int selected = programs_.getSelectedRow();
    if (selected < 0 || selected >= static_cast<int> (visible_.size()))
        return;
    const auto row = visible_[static_cast<std::size_t> (selected)];
    if (row.factory)
        processor_.loadFactoryProgram (row.index);
    else
        processor_.loadUserProgram (row.index);
}

void SynthPanel::saveNamed()
{
    const int category = category_.getSelectedId() <= 1
                           ? static_cast<int> (ProgramCategory::user)
                           : category_.getSelectedId() - 2;
    if (processor_.saveUserProgram (name_.getText().trim(), category))
        refresh();
}

void SynthPanel::resized()
{
    auto area = getLocalBounds().reduced (8);
    capacity_.setBounds (area.removeFromTop (22));
    auto row = area.removeFromTop (28);
    search_.setBounds (row.removeFromLeft (220));
    row.removeFromLeft (8);
    category_.setBounds (row.removeFromLeft (140));
    row.removeFromLeft (8);
    name_.setBounds (row.removeFromLeft (180));
    row.removeFromLeft (8);
    save_.setBounds (row.removeFromLeft (72));
    row.removeFromLeft (8);
    load_.setBounds (row.removeFromLeft (72));
    area.removeFromTop (6);
    programs_.setBounds (area.removeFromTop (120));
    area.removeFromTop (8);
    viewport_.setBounds (area);

    const int columns = std::max (1, viewport_.getWidth() / 108);
    const int cellW = 108;
    const int cellH = 96;
    int x = 0;
    int y = 0;
    int column = 0;
    for (auto& control : controls_)
    {
        if (dynamic_cast<juce::Label*> (control.get()) != nullptr)
        {
            if (column != 0)
                y += cellH;
            control->setBounds (0, y, std::max (viewport_.getWidth(), 320), 22);
            y += 24;
            column = 0;
            x = 0;
            continue;
        }
        control->setBounds (x, y, cellW - 8, cellH - 8);
        x += cellW;
        if (++column >= columns)
        {
            column = 0;
            x = 0;
            y += cellH;
        }
    }
    if (column != 0)
        y += cellH;
    page_.setSize (std::max (viewport_.getWidth(), columns * cellW), std::max (y, viewport_.getHeight()));
}

} // namespace vox::syn
