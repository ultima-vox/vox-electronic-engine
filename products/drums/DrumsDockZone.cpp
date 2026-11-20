#include "DrumsDockZone.h"
#include "Tokens.h"

namespace vox::drums {
namespace c = vox::ui::tokens::colour;
struct DrumsDockZone::Group final : juce::Component {
    struct Tabs final : juce::TabbedButtonBar {
        Tabs() : TabbedButtonBar (TabsAtTop) {}
        std::function<void (int)> changed;
        void currentTabChanged (int index, const juce::String&) override { if (changed) changed (index); }
    };
    Group (const layout::DockGroup& group, Panel panel, Height height, std::function<void (int)> changed,
           std::function<void (juce::Component&)> menu)
        : height_ (std::move (height)), panel_ (group.tabs[group.active])
    {
        addAndMakeVisible (tabs_); addAndMakeVisible (actions_); addAndMakeVisible (viewport_);
        tabs_.setName ("Dock group tabs");
        for (auto id : group.tabs) tabs_.addTab (panelName (id), c::panel, -1);
        tabs_.setCurrentTabIndex (group.active, false);
        tabs_.changed = std::move (changed);
        actions_.setButtonText ("..."); actions_.setTooltip ("Move, stack, hide or collapse this panel");
        actions_.onClick = [this, menu = std::move (menu)] { menu (actions_); };
        viewport_.setScrollBarsShown (true, true);
        viewport_.setViewedComponent (panel (panel_), false);
    }
    ~Group() override { viewport_.setViewedComponent (nullptr, false); }
    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (26);
        actions_.setBounds (header.removeFromRight (26)); tabs_.setBounds (header);
        viewport_.setBounds (area);
        if (auto* panel = viewport_.getViewedComponent())
            panel->setSize (std::max (196, area.getWidth() - viewport_.getScrollBarThickness()),
                            std::max (area.getHeight(), height_ (panel_)));
    }
    Tabs tabs_;
    juce::TextButton actions_;
    juce::Viewport viewport_;
    Height height_;
    layout::Byte panel_;
};

const char* DrumsDockZone::panelName (layout::Byte panel) noexcept
{
    switch (panel)
    {
        case 1: return "Pad Rack"; case 2: return "Library"; case 3: return "Inspector";
        case 4: return "Generator"; case 5: return "Routing"; case 6: return "Diagnostics";
        default: return "Unavailable";
    }
}
DrumsDockZone::DrumsDockZone (layout::Type side)
{
    addChildComponent (rail_);
    rail_.setButtonText (side == layout::Type::left ? ">" : "<");
    rail_.setTooltip ("Expand dock");
    rail_.onClick = [this] {
        if (! model_) return;
        auto next = *model_; next.rail = false;
        const auto commit = commit_; commit (next);
    };
}
DrumsDockZone::~DrumsDockZone() { clear(); }
void DrumsDockZone::clear()
{
    ++generation_; dragging_ = -1; groups_.clear(); rail_.setVisible (false);
}
void DrumsDockZone::configure (std::optional<layout::Dock> model, Panel panel, Height height, Commit commit, Menu menu)
{
    clear(); model_ = std::move (model); commit_ = std::move (commit);
    setVisible (model_.has_value());
    if (! model_) return;
    rail_.setVisible (model_->rail);
    if (! model_->rail)
        for (std::size_t n = 0; n < model_->groups.size(); ++n)
        {
            const auto weak = juce::Component::SafePointer<DrumsDockZone> (this);
            const auto generation = generation_;
            const auto active = model_->groups[n].tabs[model_->groups[n].active];
            auto group = std::make_unique<Group> (model_->groups[n], panel, height,
                [weak, n, generation] (int tab) {
                    // Reparent after the old tab's callback returns. Restore
                    // and close invalidate pending actions.
                    juce::MessageManager::callAsync ([weak, n, generation, tab] {
                        if (! weak || generation != weak->generation_ || ! weak->model_ || tab < 0
                            || n >= weak->model_->groups.size() || static_cast<std::size_t> (tab) >= weak->model_->groups[n].tabs.size()) return;
                        auto next = *weak->model_; next.groups[n].active = static_cast<layout::Byte> (tab);
                        const auto commit = weak->commit_; commit (next);
                    });
                }, [menu, n, active] (juce::Component& target) { menu (static_cast<unsigned> (n), active, target); });
            addAndMakeVisible (*group); groups_.push_back (std::move (group));
        }
    resized();
}
void DrumsDockZone::paint (juce::Graphics& g)
{
    g.fillAll (c::panel); g.setColour (c::border);
    for (std::size_t n = 1; n < groups_.size(); ++n) g.fillRect (0, groups_[n]->getY() - 4, getWidth(), 2);
}
void DrumsDockZone::resized()
{
    rail_.setBounds (getLocalBounds());
    if (! model_ || groups_.empty()) return;
    const int count = static_cast<int> (groups_.size());
    const int usable = std::max (0, getHeight() - (count - 1) * 6);
    const int minimum = std::min (52, usable / count);
    const int extra = usable - minimum * count;
    unsigned sum = 0;
    for (const auto& group : model_->groups) sum += group.heightPermille;
    int y = 0;
    for (std::size_t n = 0; n < groups_.size(); ++n)
    {
        const int height = n + 1 == groups_.size() ? getHeight() - y
            : minimum + static_cast<int> (static_cast<unsigned> (extra) * model_->groups[n].heightPermille / std::max (1u, sum));
        groups_[n]->setBounds (0, y, getWidth(), height); groups_[n]->resized(); y += height + 6;
    }
    repaint();
}
void DrumsDockZone::mouseDown (const juce::MouseEvent& e)
{
    dragging_ = -1;
    for (std::size_t n = 1; n < groups_.size(); ++n)
        if (e.y >= groups_[n]->getY() - 6 && e.y < groups_[n]->getY()) dragging_ = static_cast<int> (n - 1);
}
void DrumsDockZone::mouseMove (const juce::MouseEvent& e)
{
    bool divider = false;
    for (std::size_t n = 1; n < groups_.size(); ++n)
        divider = divider || (e.y >= groups_[n]->getY() - 6 && e.y < groups_[n]->getY());
    setMouseCursor (divider ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}
void DrumsDockZone::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging_ < 0 || ! model_) return;
    const auto at = static_cast<std::size_t> (dragging_);
    const auto first = groups_[at]->getBounds(), second = groups_[at + 1]->getBounds();
    const int minimum = std::min (52, (first.getHeight() + second.getHeight()) / 2);
    const int edge = juce::jlimit (first.getY() + minimum, second.getBottom() - minimum - 6, e.y);
    const int usable = getHeight() - (static_cast<int> (groups_.size()) - 1) * 6;
    if (usable <= 0) return;
    unsigned remaining = 1000;
    for (std::size_t n = 0; n < groups_.size(); ++n)
    {
        int height = groups_[n]->getHeight();
        if (n == at) height = edge - first.getY();
        if (n == at + 1) height = second.getBottom() - edge - 6;
        const auto weight = static_cast<unsigned> (std::max (0, height) * 1000 / usable);
        model_->groups[n].heightPermille = n + 1 == groups_.size() ? remaining : std::min (remaining, weight);
        remaining -= model_->groups[n].heightPermille;
    }
    resized();
}
void DrumsDockZone::mouseUp (const juce::MouseEvent&)
{
    if (dragging_ < 0 || ! model_) return;
    dragging_ = -1;
    const auto model = *model_; const auto commit = commit_; commit (model);
}
std::size_t DrumsDockZone::groupCount() const noexcept { return groups_.size(); }
juce::TabbedButtonBar* DrumsDockZone::tabs (std::size_t n) noexcept { return n < groups_.size() ? &groups_[n]->tabs_ : nullptr; }
} // namespace vox::drums
