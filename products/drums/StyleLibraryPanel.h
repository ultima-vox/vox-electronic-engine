#pragma once
#include "DrumsPages.h"
#include "StyleLibrary.h"

namespace vox::drums {

// Native library view/controller over the existing strict service. File actions
// are explicit and non-RT; update() only reflects the embedded project copy.
class StyleLibraryPanel final : public Page, private juce::ListBoxModel {
public:
    StyleLibraryPanel (DrumsProcessor&, juce::File folder = StyleLibrary::defaultFolder());
    ~StyleLibraryPanel() override;
    void update() override;
    void resized() override;
    int dockHeight() const override;
    juce::Component* named (const juce::String&) override;
    StyleLibrary::ImportResult importFile (const juce::File&);
    juce::Result exportSelection (const juce::File&);
    bool useSelected();
    void refresh (const juce::String& selectId = {});
    void selectLibraryStyle (const juce::String& id);
    // GUI qualification only: same frozen completion as the native chooser.
    // Empty in production; closing the component invalidates that completion.
    std::function<void (bool save, const juce::File& suggested, std::function<void (juce::File)>)> fileDialogForTesting;
private:
    struct Row { std::shared_ptr<const style::Definition> definition; juce::String hash; bool project = false; };
    enum class Pending { none, conflict, remove };
    const Row* selected() const;
    void rebuild (const juce::String& selectId = {});
    void buttons();
    void cancelPending();
    void resolveConflict (StyleLibrary::Collision);
    void requestRemove();
    void confirmRemove();
    void chooseImport();
    void chooseExport();
    void chooseFile (bool save, const juce::File& suggested, std::function<void (juce::File)>);
    void report (const juce::String&, bool ok = true);
    void imported (const StyleLibrary::ImportResult&);
    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
    void selectedRowsChanged (int) override;
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override;
    DrumsProcessor& processor_;
    StyleLibrary library_;
    std::vector<StyleLibrary::Entry> entries_;
    std::vector<Row> rows_;
    std::shared_ptr<const style::Definition> project_, incoming_;
    juce::String pendingId_, pendingHash_, skipped_;
    Pending pending_ = Pending::none;
    bool rebuilding_ = false;
    juce::ListBox list_ { "User styles", this };
    juce::Label title_, detail_, status_, notice_, pendingText_;
    vox::ui::VoxButton use_, import_, export_, refresh_, remove_, replace_, keepBoth_, confirmRemove_, cancel_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace vox::drums
