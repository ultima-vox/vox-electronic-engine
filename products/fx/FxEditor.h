#pragma once
#include "engine/PatternEdit.h"
#include "components/VoxKnob.h"
#include "components/VoxButton.h"
#include "components/VoxComboBox.h"
#include "components/VoxTabBar.h"
#include "components/VoxKeyboard.h"
#include "components/VoxSectionHeader.h"
#include "LayerPanel.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <deque>
#include "engine/FileExport.h"
#include "engine/ProgramStore.h"
#include "engine/PresetLibrary.h"
#include "engine/Preview.h"

namespace vox::fx {
class FxProcessor;

class GeneratorPanel final : public juce::Component, private juce::Timer {
public:
    explicit GeneratorPanel(FxProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    juce::ComboBox& styleSelector() noexcept { return style_; }
    juce::ComboBox& complexitySelector() noexcept { return complexity_; }
    juce::Slider& intensitySlider() noexcept { return intensity_; }
    juce::Slider& energySlider() noexcept { return energy_; }
    juce::Slider& stereoSlider() noexcept { return stereo_; }
    juce::ComboBox& lockLayerSelector() noexcept { return lockLayer_; }
    juce::Button& fieldLock(std::size_t index) noexcept { return fieldLocks_[index]; }
private:
    void timerCallback() override;
    FxProcessor& processor_;
    juce::ComboBox style_,complexity_,lockLayer_,role_,graphLock_;
    std::array<juce::ToggleButton,12> fieldLocks_;
    juce::ToggleButton lockGroup_{"Lock selected group"};
    juce::TextButton fullGenerate_{"Generate sound"},fullMutate_{"Mutate sound"};
    juce::Label graphStatus_;
    juce::Slider intensity_,energy_,stereo_;
    std::array<juce::Label,6> labels_;
    juce::TextButton close_ { "Close" };
};

// The Program graph is a separate editor view; these trajectories are
// structural and do not act as host automation parameters.
class ProgramCurvePanel final : public juce::Component {
public:
    explicit ProgramCurvePanel(FxProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    CurveEditor& editor() noexcept; // active scope editor
    juce::ComboBox& selector() noexcept { return selector_; }
    juce::ComboBox& scopeSelector() noexcept { return scope_; }
    static constexpr int scopeCount = 5; // Program + Layer A..D
private:
    void refreshHint();
    void applyScope();
    CurveEditor& activeEditor() noexcept;
    CurveEditor editor_;
    vox::ui::VoxComboBox selector_;
    vox::ui::VoxComboBox scope_;
    std::array<std::unique_ptr<CurveEditor>,4> layerEditors_{};
    juce::TextButton undo_ { "Undo" }, redo_ { "Redo" }, reset_ { "Reset curve" };
    juce::Label hint_;
};

// One-shot, held auditions and one fixed-tempo Sequence cycle all use the
// same worker. Options are editor-local, never DAW parameters.
class WavOptionsPanel final : public juce::Component {
public:
    WavOptionsPanel();
    void paint(juce::Graphics&) override;
    void resized() override;
    offline::Settings settings() const;
    std::function<void(offline::Settings)> onSave,onPrepareDrag;
private:
    juce::ComboBox rate_, format_;
    juce::Slider note_, velocity_;
    std::array<juce::Label,4> labels_;
    juce::TextButton close_{"Cancel"}, save_{"Choose file..."},prepare_{"Prepare drag"};
};

// Structural gestures must submit synchronously: an ordinary Slider's
// asynchronous onValueChange may be delivered after mouse-up or host restore.
class StructuralSlider final : public juce::Slider {
public:
    std::function<void()> onStructuralChange;
    void valueChanged() override { if(onStructuralChange)onStructuralChange(); }
};

class MotionPanel final : public juce::Component, private juce::ListBoxModel {
public:
    explicit MotionPanel(FxProcessor&);
    std::function<void()> onClosed; // invoked when the user closes the editor
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh(int row=-1);
    bool keyPressed(const juce::KeyPress&) override;
    juce::ComboBox& sourceSelector() noexcept {return source_;}
    juce::ComboBox& destinationSelector() noexcept {return destination_;}
    juce::TextButton& applyButton() noexcept {return apply_;}
    juce::Slider& stepLevel(std::size_t i) noexcept {return steps_[i];}
    juce::TextButton& undoButton() noexcept {return undoButton_;}
    juce::TextButton& redoButton() noexcept {return redoButton_;}
    juce::Slider& lfoRate(std::size_t i) noexcept {return lfoRate_[i];}
    juce::TextButton& applyClocksButton() noexcept {return clock_;}
    juce::TextButton& deleteRouteButton() noexcept {return remove_;}
    juce::TextButton& applyShapeButton() noexcept {return shapeApply_;}
    juce::ToggleButton& customAdsrButton() noexcept {return customAdsr_;}
    juce::Slider& adsrValue(std::size_t i) noexcept {return adsr_[i];}
private:
    int getNumRows() override;
    void paintListBoxItem(int,juce::Graphics&,int,int,bool) override;
    void selectedRowsChanged(int) override;
    void commit(int action);
    bool publishMotion(const macro::Config&);
    void stepHistory(bool redo);
    FxProcessor& processor_;
    ProgramStore::VersionedMotion shown_{};
    std::deque<macro::Config> undoMotion_,redoMotion_;
    juce::ListBox rows_{"Routes",this};
    juce::ComboBox source_,destination_;
    StructuralSlider depth_,smooth_,randomRate_,randomSeed_;
    std::array<StructuralSlider,2> lfoRate_;
    std::array<juce::ToggleButton,2> sync_;
    std::array<juce::Slider,8> steps_;
    std::array<juce::Slider,4> adsr_;
    juce::Label shapeTitle_;
    juce::ToggleButton customAdsr_{"Custom ADSR"};
    juce::TextButton shapeApply_{"Apply shape"},resetShape_{"Reset shape"};
    juce::TextButton apply_{"Apply route"},remove_{"Delete route"},clock_{"Apply clocks"},reload_{"Reload"},close_{"Close"};
    juce::TextButton undoButton_{"Undo"},redoButton_{"Redo"};
    juce::Label status_,title_,routeHelp_;
};

class SourcePanel final : public juce::Component {
public:
    explicit SourcePanel(FxProcessor&);
    void open(juce::Component& returnFocus);
    bool keyPressed(const juce::KeyPress&) override;
    void refresh();
    void paint(juce::Graphics&) override;
    void resized() override;
    juce::ComboBox& scaleSelector() noexcept { return scale_; }
    juce::ComboBox& rootSelector() noexcept { return root_; }
    juce::ComboBox& layerSelector() noexcept { return layer_; }
    juce::ComboBox& typeSelector(std::size_t slot) noexcept { return filters_[slot].type; }
    juce::Slider& cutoffSlider(std::size_t slot) noexcept { return filters_[slot].hz; }
    juce::Slider& grainLengthSlider() noexcept {return grainLength_;}
    juce::Slider& grainTravelSlider() noexcept {return grainTravel_;}
    juce::ToggleButton& grainScatterButton() noexcept {return scatter_;}
    juce::ComboBox& sliceGridSelector() noexcept {return sliceGrid_;}
    juce::ComboBox& sliceReadSelector() noexcept {return sliceRead_;}
    juce::ToggleButton& sliceGate(std::size_t i) noexcept {return sliceGate_[i];}
    juce::Slider& familyControlSlider(std::size_t i) noexcept {return familyControls_[i];}
private:
    void close();
    template <typename Edit> void editGlitch(Edit);
    struct FilterRow { juce::ComboBox type, slope; StructuralSlider hz, q; };
    FxProcessor& processor_;
    juce::ComboBox layer_, root_, scale_;
    StructuralSlider transpose_,character_,grainLength_,grainPosition_,grainTravel_;
    std::array<StructuralSlider,3> familyControls_;
    juce::ToggleButton scatter_{"Scatter"},reverse_{"Reverse"};
    juce::Label characterLabel_;
    std::array<juce::Label,3> familyControlLabels_;
    juce::ComboBox sliceGrid_,sliceRead_;
    std::array<juce::ToggleButton,4> sliceGate_;
    juce::TextButton sliceReset_{"Reset slices"};
    juce::Component::SafePointer<juce::Component> returnFocus_;
    std::array<FilterRow,2> filters_;
    juce::TextButton close_{"Close"};
    std::uint64_t revision_=0;
    bool dragging_=false;
    template <typename Edit> void editLayer(Edit);
    void editKey();
};

class PresetBrowser final : public juce::Component, private juce::ListBoxModel, private juce::Timer {
public:
    explicit PresetBrowser(FxProcessor&,juce::File root=library::userRoot());
    ~PresetBrowser() override;
    void open();
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void(bool)> looseFile;
    std::function<void()> onClosed; // invoked when the user closes the browser
    void setEmbedded(bool embedded); // workspace embed: no close button
    juce::TextEditor& searchField() noexcept {return search_;}
    juce::TextEditor& nameField() noexcept {return name_;}
    juce::TextButton& saveButton() noexcept {return save_;}
    juce::TextButton& loadButton() noexcept {return load_;}
    int matchCount() const noexcept {return static_cast<int>(visible_.size());}
    bool indexing() const noexcept {return indexJob_!=nullptr;}
    void pollBackground();
    juce::ComboBox& durationFacet() noexcept {return durationFacet_;}
    juce::ComboBox& tonalityFacet() noexcept {return tonalFacet_;}
    juce::ComboBox& styleFacet() noexcept {return styleFacet_;}
private:
    struct Row { portable::Tags tags;PlayMode mode;int factory=-1;juce::File file{};juce::String details;
        unsigned bars=0;float seconds=0;bool pitched=false;juce::String key,searchable; };
    int getNumRows() override;
    void paintListBoxItem(int,juce::Graphics&,int,int,bool) override;
    void selectedRowsChanged(int) override;
    void timerCallback() override;
    void scan();
    void rebuild(const juce::File& select={});
    void upsert(const library::IndexEntry&);
    void filter();
    void runPreview();
    void stopPreview();
    void load();
    void save(bool tagsOnly);
    void erase();
    const Row* selected() const noexcept;
    portable::Tags editorTags() const;
    void report(juce::String);
    FxProcessor& processor_;
    juce::File root_;
    std::vector<Row> all_;
    std::vector<std::size_t> visible_;
    juce::ListBox rows_{"Presets",this};
    juce::TextEditor search_,name_,style_,mood_,use_;
    juce::ComboBox scope_,role_,mode_,editRole_,styleFacet_,moodFacet_,useFacet_,durationFacet_,tonalFacet_;
    juce::ToggleButton onlyFavorites_{"Favorites"},favorite_{"Favorite"};
    juce::Label title_,detail_,status_;
    juce::TextButton load_{"Load"},play_{"Preview"},stop_{"Stop"},save_{"Save new"},tags_{"Save tags"},remove_{"Delete"},openFile_{"Open file…"},saveFile_{"Save file…"},close_{"Close"};
    std::unique_ptr<preview::Job> audition_;
    std::unique_ptr<library::IndexJob> indexJob_;
    juce::String indexStatus_;
    preview::Bus::Ticket previewTicket_=0;
    bool armed_=false;
};

// Deliberately no click or keyboard action: a native file drag needs a
// held pointer gesture. Save remains the keyboard-accessible export path.
class DragFileGrip final : public juce::TextButton {
public:
    using TextButton::TextButton;
    std::function<void()> begin;
    void mouseDown(const juce::MouseEvent& event) override {
        source_=isEnabled()&&event.mods.isLeftButtonDown()?event.source.getIndex():-1;
    }
    void mouseDrag(const juce::MouseEvent& event) override {
        if(source_==event.source.getIndex()&&isEnabled()&&event.mods.isLeftButtonDown()&&event.getDistanceFromDragStart()>=5){
            source_=-1;
            const auto run=begin;
            if(run)run();
        }
    }
    void mouseUp(const juce::MouseEvent&) override { source_=-1; }
private:
    int source_=-1;
};

// An honest FX-01 panel: only controls with working processor bindings are
// exposed. Additional workspaces are added as their DSP/controller exists.
class FxPanel final : public juce::Component, private juce::Timer {
public:
    using SaveResult=std::function<void(juce::File)>;
    // Inject the nonblocking picker in the native offscreen harness. The
    // completion is always delivered on the message thread.
    using SavePicker=std::function<void(fileexport::Kind,SaveResult)>;
    using ExternalDrag=std::function<bool(const juce::File&,juce::Component&,std::function<void()>)>;
    explicit FxPanel (FxProcessor&,SavePicker={},ExternalDrag={});
    ~FxPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    // Eight functional workspaces (TZ 13.2). One workspace shows at a time;
    // overlays (Source/Motion/Generator/Presets/WAV options) keep their
    // existing open/close behaviour on top of the current workspace.
    enum class Workspace { design=0, perform, timeline, motion, process, trigger, presets, advanced, count };
    static constexpr int workspaceCount = static_cast<int>(Workspace::count);
    void setWorkspace (Workspace workspace);
    Workspace workspace() const noexcept { return workspace_; }
    vox::ui::VoxKeyboard& keyboard() noexcept { return keyboard_; }
    vox::ui::VoxTabBar& workspaceTabs() noexcept { return workspaceTabs_; }
    // Eight visible, host-automatable macro knobs (TZ 13.2 PERFORM).
    vox::ui::VoxKnob& macroKnob(std::size_t index) noexcept { return macroKnobs_[index]; }
    juce::Slider& outputSlider() noexcept { return output_.getSlider(); }
    juce::Slider& durationSlider() noexcept { return duration_.getSlider(); }
    juce::ComboBox& comparisonSelector() noexcept { return comparison_; }
    juce::ComboBox& factorySelector() noexcept { return factory_; }
    juce::ComboBox& familySelector() noexcept { return family_; }
    juce::ComboBox& eventBarsSelector() noexcept { return eventBars_; }
    juce::Slider& macroDepthSlider() noexcept { return macroDepth_; }
    juce::Slider& macroValueSlider() noexcept { return macroValue_; }
    juce::ComboBox& macroDestinationSelector() noexcept { return macroDestination_; }
    juce::ComboBox& macroSelector() noexcept { return macroSelector_; }
    juce::Slider& seedSlider() noexcept { return seed_; }
    GeneratorPanel& generatorPanel() noexcept { return *generatorPanel_; }
    juce::Button& generateButton() noexcept { return generate_; }
    juce::Button& mutateButton() noexcept { return mutate_; }
    juce::ComboBox& patternGridSelector() noexcept { return patternGrid_; }
    juce::ComboBox& patternPageSelector() noexcept { return patternPage_; }
    juce::ComboBox& patternStepSelector() noexcept { return patternStepSelector_; }
    juce::ComboBox& patternActionsSelector() noexcept { return patternActions_; }
    juce::Slider& patternSwingSlider() noexcept { return patternSwing_; }
    juce::Slider& patternNoteSlider() noexcept { return patternDetails_[0]; }
    juce::Slider& patternVelocitySlider() noexcept { return patternDetails_[1]; }
    juce::Slider& patternProbabilitySlider() noexcept { return patternDetails_[2]; }
    juce::Slider& patternRatchetSlider() noexcept { return patternDetails_[3]; }
    juce::ComboBox& patternLengthSelector() noexcept { return patternLength_; }
    juce::ComboBox& patternBarSelector() noexcept { return patternBar_; }
    juce::Button& patternStep(std::size_t index) noexcept { return patternSteps_[index]; }
    juce::ComboBox& patternSelector() noexcept { return patternSelector_; }
    juce::Button& midiExportButton() noexcept { return midiExport_; }
    const juce::Button& wavExportButton() const noexcept { return wavExport_; }
    juce::Button& prepareMidiDragButton() noexcept { return prepareMidiDrag_; }
    DragFileGrip& dragGrip() noexcept { return dragGrip_; }
    WavOptionsPanel& wavOptionsPanel() noexcept { return wavOptions_; }
    juce::Button& sampleImportButton() noexcept { return sampleImport_; }
    juce::Button& sampleBuiltinButton() noexcept { return sampleBuiltin_; }
    juce::Button& patternButton() noexcept { return patternEnabled_; }
    juce::Button& layerLock(std::size_t index) noexcept { return layerLocks_[index]; }
    juce::ComboBox& effectSelector(std::size_t index) noexcept { return effectsType_[index]; }
    juce::Slider& effectWet(std::size_t index) noexcept { return effectsWet_[index]; }
    SourcePanel& sourcePanel() noexcept { return sourcePanel_; }
    MotionPanel& motionPanel() noexcept { return motionPanel_; }
    ProgramCurvePanel& programCurves() noexcept { return programCurves_; }
    juce::Button& programCurvesButton() noexcept { return programCurvesButton_; }
    LayerPanel& layerPanel(std::size_t index) noexcept { return *layers_[index]; }
private:
    void timerCallback() override;
    void focusLost (juce::Component::FocusChangeType cause) override;
    void applyWorkspaceVisibility();
    void importSample();
    void exportPattern(bool drag=false);
    void beginFileDrag();
    void choosePreset(bool saving);
    void exportWav(offline::Settings,bool drag=false);
    void chooseExportFile(fileexport::Kind,SaveResult);
    void refreshExportControls();
    void refreshPattern();
    template <typename Edit> void editPattern(Edit);
    void patternCommand(int);
    void notePatternUndo(const pattern::Pattern&);
    void finishPatternDrag();
    void refreshMacro();
    void refreshGenerator();
    void refreshEffectSemantics(std::size_t);
    FxProcessor& processor_;
    SavePicker savePicker_;
    ExternalDrag externalDrag_;
    vox::ui::VoxKnob output_ { "OUTPUT", vox::ui::VoxKnob::Size::Large };
    juce::SliderParameterAttachment attachment_;
    vox::ui::VoxKnob duration_ { "DURATION (S)", vox::ui::VoxKnob::Size::Large };
    juce::SliderParameterAttachment durationAttachment_;
    vox::ui::VoxComboBox family_,eventBars_,playMode_,triggerPolicy_,macroSelector_,macroDestination_,factory_,comparison_;
    juce::Label clockStatus_,fxTailStatus_;
    juce::Slider macroValue_,macroDepth_;
    std::unique_ptr<juce::SliderParameterAttachment> macroAttachment_;
    vox::ui::VoxButton audition_ { "AUDITION" }, stopAudition_ { "STOP" };
    vox::ui::VoxButton panic_ { "PANIC" };
    juce::Slider seed_;
    vox::ui::VoxButton generate_ { "GENERATE" };
    vox::ui::VoxButton mutate_ { "MUTATE" };
    juce::TextButton generatorSettings_ { "Settings" };
    std::unique_ptr<GeneratorPanel> generatorPanel_;
    vox::ui::VoxButton midiExport_ { "MIDI" };
    vox::ui::VoxButton wavExport_ { "WAV" };
    juce::TextButton prepareMidiDrag_{"Prepare MIDI drag"};
    DragFileGrip dragGrip_{"No drag file"};
    juce::Label dragWarning_;
    bool dragFailed_=false;
    WavOptionsPanel wavOptions_;
    juce::TextButton sampleImport_ { "Import" },sampleBuiltin_ { "Built-in" };
    juce::Label sampleStatus_;
    std::unique_ptr<juce::FileChooser> sampleChooser_;
    std::unique_ptr<juce::FileChooser> exportChooser_;
    bool exportChooserOpen_=false;
    std::uint64_t exportChooserTicket_=0;
    std::array<juce::ToggleButton,4> layerLocks_;
    vox::ui::VoxComboBox patternSelector_,patternLength_,patternBar_,patternStepSelector_,patternGrid_,patternPage_;
    std::array<StructuralSlider,4> patternDetails_;
    StructuralSlider patternSwing_;
    juce::ComboBox patternActions_;
    std::deque<pattern::Pattern> patternUndo_,patternRedo_;
    pattern::Pattern displayedPattern_,patternDragBefore_;
    pattern::edit::Bar copiedPatternBar_;
    std::uint64_t patternRevision_=0;
    bool patternDragging_=false;
    std::array<juce::TextButton,16> patternSteps_;
    vox::ui::VoxButton patternEnabled_ { "HOST PATTERN", vox::ui::VoxButton::Type::Toggle };
    std::unique_ptr<juce::ButtonParameterAttachment> patternAttachment_;
    std::array<std::unique_ptr<LayerPanel>, 4> layers_;
    juce::TextButton programCurvesButton_ { "Program curves" };
    ProgramCurvePanel programCurves_;
    juce::TextButton sourceButton_{"Source / Key"};
    SourcePanel sourcePanel_;
    juce::TextButton motionButton_{"Motion"};
    MotionPanel motionPanel_;
    juce::TextButton presets_{"Presets"};
    PresetBrowser browser_;
    std::unique_ptr<juce::FileChooser> presetChooser_;
    std::array<vox::ui::VoxComboBox,4> effectsType_;
    std::array<juce::Slider,4> effectsWet_, effectsAmount_;
    std::array<std::unique_ptr<juce::SliderParameterAttachment>,4> wetAttachments_,amountAttachments_;
    std::array<float,2> shownPeaks_{};
    unsigned shownEvents_=0,shownGrains_=0,clipHold_=0,fxTailHold_=0;
    std::uint32_t shownDropped_=0;
    // Workspace shell: tab strip, section header, persistent keyboard.
    vox::ui::VoxTabBar workspaceTabs_;
    vox::ui::VoxKeyboard keyboard_;
    std::array<vox::ui::VoxKnob,8> macroKnobs_ {};
    std::array<std::unique_ptr<juce::SliderParameterAttachment>,8> macroKnobAttachments_;
    vox::ui::VoxSectionHeader workspaceHeader_;
    Workspace workspace_ = Workspace::design;
    bool syncingTabs_ = false;
    juce::Rectangle<int> meterBounds_, macroLabelBounds_, fxLabelBounds_;
};
} // namespace vox::fx
