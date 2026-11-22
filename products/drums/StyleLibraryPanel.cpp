#include "StyleLibraryPanel.h"
#include "Processor.h"
#include "Tokens.h"
#include "Typography.h"

namespace vox::drums {
namespace { namespace c = vox::ui::tokens::colour; }

StyleLibraryPanel::StyleLibraryPanel (DrumsProcessor& p, juce::File folder) : processor_ (p), library_ (std::move (folder))
{
    title_.setText ("User styles / projects keep their own copy", juce::dontSendNotification);
    for (auto* label : { &title_, &detail_, &status_, &notice_, &pendingText_ })
    { addAndMakeVisible (*label); label->setJustificationType (juce::Justification::topLeft); label->setFont (vox::ui::typography::valueText()); }
    addAndMakeVisible (list_); list_.setRowHeight (28); list_.setColour (juce::ListBox::backgroundColourId, c::panel);
    const auto action = [this] (vox::ui::VoxButton& b, const char* text, std::function<void()> fn) {
        b.setButtonText (text); b.onClick = std::move (fn); addAndMakeVisible (b);
    };
    action (use_, "Use style", [this] { useSelected(); }); use_.setType (vox::ui::VoxButton::Type::Primary);
    action (import_, "Import...", [this] { chooseImport(); });
    action (export_, "Export...", [this] { chooseExport(); });
    action (refresh_, "Refresh", [this] { cancelPending(); refresh(); });
    action (remove_, "Remove...", [this] { requestRemove(); });
    action (replace_, "Replace", [this] { resolveConflict (StyleLibrary::Collision::replace); });
    action (keepBoth_, "Keep both", [this] { resolveConflict (StyleLibrary::Collision::keepBoth); });
    action (confirmRemove_, "Remove", [this] { confirmRemove(); }); confirmRemove_.setType (vox::ui::VoxButton::Type::Danger);
    action (cancel_, "Cancel", [this] { cancelPending(); });
    refresh();
}
StyleLibraryPanel::~StyleLibraryPanel() = default;
const StyleLibraryPanel::Row* StyleLibraryPanel::selected() const
{
    const int n = list_.getSelectedRow(); return n >= 0 && n < static_cast<int> (rows_.size()) ? &rows_[static_cast<std::size_t> (n)] : nullptr;
}
void StyleLibraryPanel::report (const juce::String& text, bool ok)
{ status_.setText (text, juce::dontSendNotification); status_.setTooltip (text); status_.setColour (juce::Label::textColourId, ok ? c::text : juce::Colours::orange); }
void StyleLibraryPanel::refresh (const juce::String& selectId)
{
    juce::StringArray skipped; entries_ = library_.list (&skipped); skipped_ = skipped.joinIntoString ("\n");
    project_ = processor_.generator().custom; rebuild (selectId);
    if (skipped.isEmpty()) report (juce::String (static_cast<int> (entries_.size())) + " user styles");
    else report ("Skipped invalid/duplicate files:\n" + skipped_, false);
}
void StyleLibraryPanel::rebuild (const juce::String& selectId)
{
    juce::String id = selectId; bool project = false;
    if (id.isEmpty()) if (const auto* row = selected()) { id = row->definition->id; project = row->project; }
    rebuilding_ = true; rows_.clear();
    if (project_) rows_.push_back ({ project_, style::contentHash (*project_), true });
    for (const auto& entry : entries_) rows_.push_back ({ entry.definition, entry.contentHash, false });
    list_.updateContent(); list_.deselectAllRows();
    for (int n = 0; n < static_cast<int> (rows_.size()); ++n)
        if (rows_[static_cast<std::size_t> (n)].definition->id == id && rows_[static_cast<std::size_t> (n)].project == project) { list_.selectRow (n); break; }
    rebuilding_ = false; buttons();
}
void StyleLibraryPanel::selectLibraryStyle (const juce::String& id)
{
    for (int n = 0; n < static_cast<int> (rows_.size()); ++n)
        if (! rows_[static_cast<std::size_t> (n)].project && rows_[static_cast<std::size_t> (n)].definition->id == id) { list_.selectRow (n); return; }
    list_.deselectAllRows();
}
void StyleLibraryPanel::update()
{
    const auto current = processor_.generator().custom;
    if (current != project_) { cancelPending(); project_ = current; rebuild(); }
}
void StyleLibraryPanel::buttons()
{
    const auto* row = selected(); const bool idle = pending_ == Pending::none;
    use_.setEnabled (idle && row && ! row->project); export_.setEnabled (idle && row); remove_.setEnabled (idle && row && ! row->project);
    juce::String detail;
    if (row) detail = (row->project ? "Project copy: " : "Library: ") + row->definition->name + "\nAuthor: " + row->definition->meta.author
                     + "\nID: " + row->definition->id + "\nVersion: " + row->hash.substring (0, 12);
    detail_.setText (detail, juce::dontSendNotification); detail_.setTooltip (detail);
    juce::String notice = "Import/selection does not generate or change patterns.";
    if (project_) for (const auto& entry : entries_)
        if (entry.definition->id == project_->id && entry.contentHash != style::contentHash (*project_))
            notice = "This project uses its own version of " + project_->name;
    notice_.setText (notice, juce::dontSendNotification);
    pendingText_.setVisible (! idle); cancel_.setVisible (! idle);
    replace_.setVisible (pending_ == Pending::conflict); keepBoth_.setVisible (pending_ == Pending::conflict); confirmRemove_.setVisible (pending_ == Pending::remove);
    resized(); if (onDockSizeChanged) onDockSizeChanged();
}
void StyleLibraryPanel::cancelPending()
{ pending_ = Pending::none; incoming_.reset(); pendingId_.clear(); pendingHash_.clear(); buttons(); }
StyleLibrary::ImportResult StyleLibraryPanel::importFile (const juce::File& file)
{ cancelPending(); const auto result = library_.importFile (file); imported (result); return result; }
void StyleLibraryPanel::imported (const StyleLibrary::ImportResult& result)
{
    using Status = StyleLibrary::ImportResult::Status;
    if (result.status == Status::conflict && result.incoming && result.definition)
    {
        pending_ = Pending::conflict; incoming_ = result.incoming; pendingId_ = result.definition->id; pendingHash_ = style::contentHash (*result.definition);
        pendingText_.setText (result.message + "\nIncoming: " + incoming_->name + "; projects retain their embedded copy.", juce::dontSendNotification); buttons();
    }
    else if (result.status != Status::refused && result.status != Status::conflict) refresh (result.definition ? result.definition->id : juce::String {});
    report (result.message, result.status != Status::refused);
}
void StyleLibraryPanel::resolveConflict (StyleLibrary::Collision choice)
{
    if (pending_ != Pending::conflict || ! incoming_) return;
    const auto existing = library_.find (pendingId_);
    if (! existing || style::contentHash (*existing) != pendingHash_)
    { cancelPending(); refresh(); report ("Library entry changed. Import again before choosing Replace or Keep both.", false); return; }
    const auto text = style::serialize (*incoming_); cancelPending(); imported (library_.importText (text, choice));
}
bool StyleLibraryPanel::useSelected()
{
    const auto* row = selected(); if (! row || row->project || pending_ != Pending::none) return false;
    const auto epoch = processor_.patternEpoch(); const auto id = row->definition->id; const auto hash = row->hash;
    const auto definition = library_.find (id);
    if (! definition || style::contentHash (*definition) != hash) { report ("Selected style changed or is unavailable. Refresh before using it.", false); return false; }
    if (! processor_.useUserStyle (definition, epoch)) { report ("Project changed; choose the style again.", false); return false; }
    update(); report ("Using " + definition->name + "; patterns are unchanged"); return true;
}
juce::Result StyleLibraryPanel::exportSelection (const juce::File& file)
{
    const auto* row = selected(); const auto result = row ? StyleLibrary::exportStyle (*row->definition, file) : juce::Result::fail ("Select a style first");
    report (result.wasOk() ? "Style exported" : result.getErrorMessage(), result.wasOk()); return result;
}
void StyleLibraryPanel::requestRemove()
{
    const auto* row = selected(); if (! row || row->project || pending_ != Pending::none) return;
    pendingId_ = row->definition->id; pendingHash_ = row->hash; pending_ = Pending::remove;
    pendingText_.setText ("Remove " + row->definition->name + " from this library?\nProjects keep their embedded copy.", juce::dontSendNotification); buttons();
}
void StyleLibraryPanel::confirmRemove()
{
    if (pending_ != Pending::remove) return;
    const auto existing = library_.find (pendingId_);
    if (! existing || style::contentHash (*existing) != pendingHash_) { cancelPending(); refresh(); report ("Library entry changed. Select it again before removing.", false); return; }
    const bool removed = library_.remove (pendingId_); cancelPending(); refresh(); report (removed ? "Removed from library; project copies are unchanged" : "Cannot remove this library file", removed);
}
void StyleLibraryPanel::chooseImport()
{
    juce::Component::SafePointer<StyleLibraryPanel> self (this);
    chooseFile (false, {}, [self] (juce::File file) { if (self && file != juce::File()) self->importFile (file); });
}
void StyleLibraryPanel::chooseExport()
{
    const auto* row = selected(); if (! row || pending_ != Pending::none) return;
    const auto frozen = row->definition;
    const auto name = juce::File::createLegalFileName (frozen->name) + ".voxstyle";
    juce::Component::SafePointer<StyleLibraryPanel> self (this);
    chooseFile (true, juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name),
        [self, frozen] (juce::File file) {
            if (! self || file == juce::File()) return;
            const auto result = StyleLibrary::exportStyle (*frozen, file.withFileExtension ("voxstyle"));
            self->report (result.wasOk() ? "Style exported" : result.getErrorMessage(), result.wasOk());
        });
}
void StyleLibraryPanel::chooseFile (bool save, const juce::File& suggested, std::function<void (juce::File)> completed)
{
    if (fileDialogForTesting) { fileDialogForTesting (save, suggested, std::move (completed)); return; }
    chooser_ = std::make_unique<juce::FileChooser> (save ? "Export a user style" : "Import a user style", suggested, "*.voxstyle");
    const int chooserFlags = juce::FileBrowserComponent::canSelectFiles | (save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                                                                              : juce::FileBrowserComponent::openMode);
    chooser_->launchAsync (chooserFlags, [completed = std::move (completed)] (const juce::FileChooser& fc) { completed (fc.getResult()); });
}
int StyleLibraryPanel::getNumRows() { return static_cast<int> (rows_.size()); }
void StyleLibraryPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selectedRow)
{
    if (row < 0 || row >= static_cast<int> (rows_.size())) return;
    if (selectedRow) { g.setColour (c::accent.withAlpha (0.3f)); g.fillAll(); }
    const auto& item = rows_[static_cast<std::size_t> (row)]; g.setColour (c::text); g.setFont (vox::ui::typography::valueText());
    g.drawText ((item.project ? "Project: " : "") + item.definition->name + " / " + item.definition->meta.author + " / " + item.definition->id.substring (0, 8),
                4, 0, width - 8, height, juce::Justification::centredLeft);
}
void StyleLibraryPanel::selectedRowsChanged (int) { if (! rebuilding_) cancelPending(); }
void StyleLibraryPanel::listBoxItemDoubleClicked (int, const juce::MouseEvent&) { useSelected(); }
int StyleLibraryPanel::dockHeight() const { return pending_ == Pending::none ? 620 : 760; }
void StyleLibraryPanel::resized()
{
    auto r = getLocalBounds().reduced (6); title_.setBounds (r.removeFromTop (36));
    auto row = r.removeFromTop (32); const int third = row.getWidth() / 3;
    for (auto* b : { &use_, &import_, &export_ }) b->setBounds (row.removeFromLeft (third).reduced (1));
    row = r.removeFromTop (32); refresh_.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (1)); remove_.setBounds (row.reduced (1));
    if (pending_ != Pending::none)
    {
        pendingText_.setBounds (r.removeFromTop (96)); row = r.removeFromTop (32); const int w = row.getWidth() / 3;
        if (pending_ == Pending::conflict) { replace_.setBounds (row.removeFromLeft (w).reduced (1)); keepBoth_.setBounds (row.removeFromLeft (w).reduced (1)); }
        else confirmRemove_.setBounds (row.removeFromLeft (2 * w).reduced (1));
        cancel_.setBounds (row.reduced (1));
    }
    list_.setBounds (r.removeFromTop (220)); detail_.setBounds (r.removeFromTop (132));
    status_.setBounds (r.removeFromTop (80)); notice_.setBounds (r);
}
juce::Component* StyleLibraryPanel::named (const juce::String& name)
{
    if (name == "style.list") return &list_;
    if (name == "style.status") return &status_;
    if (name == "style.notice") return &notice_;
    if (name == "style.import") return &import_;
    if (name == "style.export") return &export_;
    if (name == "style.use") return &use_;
    if (name == "style.refresh") return &refresh_;
    if (name == "style.remove") return &remove_;
    if (name == "style.replace") return &replace_;
    if (name == "style.keepBoth") return &keepBoth_;
    if (name == "style.cancel") return &cancel_;
    if (name == "style.confirmRemove") return &confirmRemove_;
    return nullptr;
}

namespace {
class NativeLibraryDock final : public Page {
public:
    NativeLibraryDock (DrumsProcessor& p, std::function<int()> pad, juce::File folder)
        : presets_ (makePresetsPage (p, std::move (pad))), styles_ (p, folder == juce::File() ? StyleLibrary::defaultFolder() : folder)
    {
        category_.addItemList ({ "Musical presets", "User styles" }, 1); category_.setSelectedId (1, juce::dontSendNotification);
        addAndMakeVisible (category_); addAndMakeVisible (*presets_); addChildComponent (styles_);
        category_.onChange = [this] { const bool styles = category_.getSelectedId() == 2; presets_->setVisible (! styles); styles_.setVisible (styles); update(); resized(); if (onDockSizeChanged) onDockSizeChanged(); };
        styles_.onDockSizeChanged = [this] { if (onDockSizeChanged) onDockSizeChanged(); };
    }
    void update() override { if (category_.getSelectedId() == 2) styles_.update(); else presets_->update(); }
    int dockHeight() const override { return 40 + (category_.getSelectedId() == 2 ? styles_.dockHeight() : 900); }
    void resized() override { auto r = getLocalBounds().reduced (4); category_.setBounds (r.removeFromTop (30)); r.removeFromTop (6); presets_->setBounds (r); styles_.setBounds (r); }
    juce::Component* named (const juce::String& name) override
    { if (name == "library.category") return &category_; if (name == "styles") return &styles_; if (name.startsWith ("style.")) return styles_.named (name); return presets_->named (name); }
private:
    std::unique_ptr<Page> presets_;
    StyleLibraryPanel styles_;
    vox::ui::VoxComboBox category_;
};
}
std::unique_ptr<Page> makeNativeLibraryDock (DrumsProcessor& p, std::function<int()> pad, juce::File folder)
{ return std::make_unique<NativeLibraryDock> (p, std::move (pad), std::move (folder)); }
} // namespace vox::drums
