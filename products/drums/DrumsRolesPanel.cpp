#include "DrumsRolesPanel.h"
#include "Processor.h"
#include "StyleRegistry.h"
#include "Tokens.h"

namespace vox::drums {
namespace {
class GeneratorDock final : public Page {
public:
    explicit GeneratorDock (DrumsProcessor& p) : processor_ (p), generator_ (makeGeneratePage (p))
    {
        addAndMakeVisible (*generator_);
        settings_.setType (vox::ui::VoxButton::Type::Toggle); roles_.setType (vox::ui::VoxButton::Type::Toggle);
        settings_.setToggleState (true, juce::dontSendNotification);
        settings_.setButtonText ("Generator"); roles_.setButtonText ("Pad roles");
        settings_.onClick = [this] { showRoles (false); }; roles_.onClick = [this] { showRoles (true); };
        addAndMakeVisible (settings_); addAndMakeVisible (roles_); addChildComponent (roleContent_);
        addChildComponent (noticeSummary_);
        noticeSummary_.onClick = [this] { showRoles (true); };
        for (int n = 0; n < 16; ++n)
        {
            auto& label = labels_[static_cast<std::size_t> (n)];
            label.setText ("Pad " + juce::String (n + 1), juce::dontSendNotification);
            roleContent_.addAndMakeVisible (label);
            auto& combo = choices_[static_cast<std::size_t> (n)];
            combo.setName ("Pad " + juce::String (n + 1) + " generator role");
            for (int role = 0; role < padRoleCount; ++role) combo.addItem (padRoleName (static_cast<PadRole> (role)), role + 1);
            combo.setTextWhenNothingSelected ("Choose role...");
            combo.onChange = [this, n] {
                if (refreshing_) return;
                const int id = choices_[static_cast<std::size_t> (n)].getSelectedId();
                if (id < 1) return;
                const auto role = static_cast<PadRole> (id - 1);
                if (preview_) preview_->choose (n, role);
                else processor_.setPadRole (n, role);
                refresh();
            };
            roleContent_.addAndMakeVisible (combo);
        }
        propose_.setButtonText ("Preview roles from models");
        propose_.onClick = [this] {
            preview_ = processor_.proposeRoles();
            previewBase_ = processor_.padRoles();
            previewGeneration_ = processor_.kitStore().generation(); previewEpoch_ = processor_.patternEpoch(); refresh();
        };
        apply_.setButtonText ("Apply preview");
        apply_.onClick = [this] {
            if (! preview_ || ! preview_->complete()) return;
            if (! currentPreview()) { preview_.reset(); status_.setText ("Kit changed; preview again", juce::dontSendNotification); refresh(); return; }
            processor_.applyRoleProposal (*preview_); preview_.reset(); refresh();
        };
        cancel_.setButtonText ("Cancel preview"); cancel_.onClick = [this] { preview_.reset(); refresh(); };
        undo_.setButtonText ("Undo"); redo_.setButtonText ("Redo");
        undo_.onClick = [this] { preview_.reset(); processor_.patternHistory().undo (processor_.patterns()); refresh(); };
        redo_.onClick = [this] { preview_.reset(); processor_.patternHistory().redo (processor_.patterns()); refresh(); };
        for (auto* component : std::initializer_list<juce::Component*> { &propose_, &apply_, &cancel_, &undo_, &redo_, &status_, &notices_ })
            roleContent_.addAndMakeVisible (*component);
        notices_.setJustificationType (juce::Justification::topLeft);
        refresh();
    }
    int dockHeight() const override { return rolesShown_ ? std::max (2200, 1600 + static_cast<int> (actions_.size()) * 64) : 1200; }
    void showRoles (bool show)
    {
        rolesShown_ = show; roleContent_.setVisible (show); generator_->setVisible (! show);
        settings_.setToggleState (! show, juce::dontSendNotification); roles_.setToggleState (show, juce::dontSendNotification);
        if (onDockSizeChanged) onDockSizeChanged();
        resized();
    }
    void update() override
    {
        generator_->update();
        if (preview_ && ! currentPreview()) { preview_.reset(); status_.setText ("Kit changed; preview again", juce::dontSendNotification); }
        refresh();
    }
    void paint (juce::Graphics& g) override { g.fillAll (vox::ui::tokens::colour::panel); }
    void resized() override
    {
        auto r = getLocalBounds(); auto tabs = r.removeFromTop (28);
        settings_.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2).reduced (1)); roles_.setBounds (tabs.reduced (1));
        noticeSummary_.setVisible (! rolesShown_ && noticeText_.isNotEmpty());
        if (noticeSummary_.isVisible()) noticeSummary_.setBounds (r.removeFromTop (44).reduced (2));
        generator_->setBounds (r); roleContent_.setBounds (r);
        auto area = roleContent_.getLocalBounds().reduced (6);
        propose_.setBounds (area.removeFromTop (34)); status_.setBounds (area.removeFromTop (48));
        for (std::size_t n = 0; n < choices_.size(); ++n)
        {
            labels_[n].setBounds (area.removeFromTop (20)); choices_[n].setBounds (area.removeFromTop (30)); area.removeFromTop (4);
        }
        for (auto* button : { &apply_, &cancel_, &undo_, &redo_ }) button->setBounds (area.removeFromTop (32).reduced (1));
        for (auto& action : actions_) action->setBounds (area.removeFromTop (32).reduced (1));
        notices_.setBounds (area);
    }
    juce::Component* named (const juce::String& name) override
    {
        if (name == "roles.view") return &roles_;
        if (name == "roles.settings") return &settings_;
        if (name == "roles.preview") return &propose_;
        if (name == "roles.apply") return &apply_;
        if (name == "roles.cancel") return &cancel_;
        if (name == "roles.undo") return &undo_;
        if (name == "roles.redo") return &redo_;
        if (name == "roles.status") return &status_;
        if (name == "roles.notices") return &notices_;
        if (name == "roles.summary") return &noticeSummary_;
        if (name.startsWith ("role."))
        {
            const int n = name.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1;
            if (n >= 0 && n < 16) return &choices_[static_cast<std::size_t> (n)];
        }
        for (auto& action : actions_) if (action->getName() == name) return action.get();
        return generator_->named (name);
    }
private:
    bool currentPreview() const
    {
        return previewGeneration_ == processor_.kitStore().generation() && previewEpoch_ == processor_.patternEpoch()
            && previewBase_ == processor_.padRoles();
    }
    void refresh()
    {
        refreshing_ = true;
        const auto current = processor_.padRoles();
        for (std::size_t n = 0; n < choices_.size(); ++n)
        {
            const auto role = preview_ ? preview_->role[n] : current.role[n];
            choices_[n].setSelectedId (preview_ && preview_->needsChoice[n] ? 0 : static_cast<int> (role) + 1, juce::dontSendNotification);
        }
        apply_.setEnabled (preview_ && preview_->complete()); cancel_.setEnabled (preview_.has_value());
        undo_.setEnabled (processor_.patternHistory().canUndo()); redo_.setEnabled (processor_.patternHistory().canRedo());
        if (preview_)
        {
            int pending = 0, changed = 0;
            for (bool row : preview_->needsChoice) if (row) ++pending;
            auto mask = preview_->changedPads (current);
            for (; mask != 0; mask = static_cast<std::uint16_t> (mask & (mask - 1))) ++changed;
            status_.setText ("Preview: " + juce::String (changed) + " changed / " + juce::String (pending) + " choices needed", juce::dontSendNotification);
        }
        else if (! status_.getText().startsWith ("Kit changed")) status_.setText ("Roles change generation, never MIDI notes", juce::dontSendNotification);
        const auto settings = processor_.generator();
        const auto* definition = settings.custom ? settings.custom.get() : style::factoryDefinition (static_cast<int> (settings.style));
        const auto notices = roleNotices (current, definition, settings.custom ? settings.custom->name : styleName (settings.style));
        juce::String text;
        for (const auto& notice : notices) text << notice.text << "\n";
        // Rebuild action components only when the displayed notice set changed.
        if (text != noticeText_)
        {
            noticeText_ = text; actions_.clear();
            for (const auto& notice : notices)
                if (notice.kind == RoleNotice::Kind::duplicate || notice.kind == RoleNotice::Kind::missingRequired)
                {
                    const bool duplicate = notice.kind == RoleNotice::Kind::duplicate;
                    auto button = std::make_unique<vox::ui::VoxButton> (duplicate ? "Use pad " + juce::String (notice.otherPad + 1)
                                                                                              : "Assign " + juce::String (padRoleName (notice.role)) + "...");
                    button->setName (duplicate ? "roles.use." + juce::String (notice.otherPad + 1)
                                              : "roles.assign." + juce::String (static_cast<int> (notice.role)));
                    button->setTooltip (notice.actionTip);
                    button->onClick = [this, notice, duplicate] {
                        const auto now = processor_.padRoles();
                        if (! duplicate)
                        {
                            showRoles (true);
                            int target = 0;
                            for (int pad = 0; pad < 16; ++pad) if (now.role[static_cast<std::size_t> (pad)] == PadRole::none) { target = pad; break; }
                            auto& choice = choices_[static_cast<std::size_t> (target)];
                            if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
                                viewport->setViewPosition (0, getLocalArea (&choice, choice.getLocalBounds()).getY());
                            choice.grabKeyboardFocus(); // explicit user choice; never assign a guessed pad
                            return;
                        }
                        if (now.role[static_cast<std::size_t> (notice.pad)] == notice.role
                            && now.role[static_cast<std::size_t> (notice.otherPad)] == notice.role)
                        {
                            int earlier = 0;
                            for (int pad = 0; pad < notice.otherPad; ++pad) if (now.role[static_cast<std::size_t> (pad)] == notice.role) ++earlier;
                            if (earlier != 1)
                            {
                                status_.setText ("Roles changed; choose the current duplicate notice", juce::dontSendNotification);
                                return;
                            }
                            processor_.setPadRoles (usePad (now.role, notice.pad, notice.otherPad));
                        }
                    };
                    roleContent_.addAndMakeVisible (*button); actions_.push_back (std::move (button));
                }
            notices_.setText (text, juce::dontSendNotification);
            noticeSummary_.setButtonText ("Roles: " + juce::String (notices.size()) + " notices / Assign...");
            noticeSummary_.setTooltip (text);
            if (onDockSizeChanged) onDockSizeChanged();
            resized();
        }
        refreshing_ = false;
    }
    DrumsProcessor& processor_;
    std::unique_ptr<Page> generator_;
    juce::Component roleContent_;
    vox::ui::VoxButton settings_, roles_, propose_, apply_, cancel_, undo_, redo_, noticeSummary_;
    std::array<juce::Label, 16> labels_;
    std::array<vox::ui::VoxComboBox, 16> choices_;
    juce::Label status_, notices_;
    std::optional<RoleProposal> preview_;
    PadRoles previewBase_;
    std::uint64_t previewGeneration_ = 0, previewEpoch_ = 0;
    bool rolesShown_ = false, refreshing_ = false;
    juce::String noticeText_;
    std::vector<std::unique_ptr<vox::ui::VoxButton>> actions_;
};
} // namespace
std::unique_ptr<Page> makeGeneratorDock (DrumsProcessor& p) { return std::make_unique<GeneratorDock> (p); }
} // namespace vox::drums
