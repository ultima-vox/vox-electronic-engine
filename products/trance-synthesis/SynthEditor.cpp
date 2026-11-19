#include "SynthEditor.h"

#include "FactoryBank.h"
#include "Parameters.h"
#include "Processor.h"

namespace vox::syn {

bool userCategoryMatchesRole (int category, int role)
{
    switch (role)
    {
        case static_cast<int> (FactoryRole::bass): return category == static_cast<int> (ProgramCategory::bass);
        case static_cast<int> (FactoryRole::acid): return category == static_cast<int> (ProgramCategory::acid);
        case static_cast<int> (FactoryRole::lead): return category == static_cast<int> (ProgramCategory::lead);
        case static_cast<int> (FactoryRole::pluckKeys):
            return category == static_cast<int> (ProgramCategory::pluck)
                || category == static_cast<int> (ProgramCategory::keys);
        case static_cast<int> (FactoryRole::padAtmos):
            return category == static_cast<int> (ProgramCategory::pad)
                || category == static_cast<int> (ProgramCategory::atmos);
        default:
            return false;
    }
}

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
    for (std::size_t i = 0; i < factoryRoleNames.size(); ++i)
        category_.addItem (factoryRoleNames[i], static_cast<int> (i) + 2);
    category_.addItem ("User", static_cast<int> (FactoryRole::count) + 2);
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
    const auto category = row.factory
        ? juce::String (factoryRoleNames[static_cast<std::size_t> (processor_.factoryProgramRole (row.index))])
        : juce::String (categoryNames[static_cast<std::size_t> (processor_.userProgramCategory (row.index))]);
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
    const int userId = static_cast<int> (FactoryRole::count) + 2;
    const int wantedRole = selected >= 2 && selected < userId ? selected - 2 : -1;
    const bool userOnly = selected == userId;
    for (int i = 0; i < processor_.factoryProgramCount(); ++i)
    {
        if (userOnly)
            continue;
        if (wantedRole >= 0 && processor_.factoryProgramRole (i) != wantedRole)
            continue;
        if (query.isNotEmpty() && ! processor_.factoryProgramSearch (i).toLowerCase().contains (query))
            continue;
        visible_.push_back ({ true, i });
    }
    for (int i = 0; i < processor_.userProgramCount(); ++i)
    {
        if (wantedRole >= 0 && ! userCategoryMatchesRole (processor_.userProgramCategory (i), wantedRole))
            continue;
        if (query.isNotEmpty() && ! processor_.userProgramName (i).toLowerCase().contains (query))
            continue;
        visible_.push_back ({ false, i });
    }
    capacity_.setText ("Installed " + juce::String (factoryProgramsInstalled)
                           + " candidate, human approved " + juce::String (factoryProgramsHumanApproved)
                           + ". Phrases installed " + juce::String (factoryPhrasesInstalled)
                           + ", human approved " + juce::String (factoryPhrasesHumanApproved)
                           + ". User " + juce::String (processor_.userProgramCount()),
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
    const int selected = category_.getSelectedId();
    const int userId = static_cast<int> (FactoryRole::count) + 2;
    int category = static_cast<int> (ProgramCategory::user);
    if (selected >= 2 && selected < userId)
    {
        switch (selected - 2)
        {
            case static_cast<int> (FactoryRole::bass): category = static_cast<int> (ProgramCategory::bass); break;
            case static_cast<int> (FactoryRole::acid): category = static_cast<int> (ProgramCategory::acid); break;
            case static_cast<int> (FactoryRole::lead): category = static_cast<int> (ProgramCategory::lead); break;
            case static_cast<int> (FactoryRole::pluckKeys): category = static_cast<int> (ProgramCategory::pluck); break;
            case static_cast<int> (FactoryRole::padAtmos): category = static_cast<int> (ProgramCategory::pad); break;
            default: break;
        }
    }
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
