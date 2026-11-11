#pragma once

// FOUND-01 shell shared by the three ULTIMA VOX MUSIC plugins: a plain JUCE
// AudioProcessor with a stereo main output, MIDI input, a versioned VOXS state
// and a VOX-styled editor. A product without an engine outputs silence; a
// product with one overrides processBlock and the product-state hooks below
// (DRM-01 for Drums; SYN-01 / FX-01 follow). Header-only so each product compiles it into its own plugin; the
// products share no runtime code or DLL.

#include <juce_audio_processors/juce_audio_processors.h>

#include "StateContainer.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace vox::suite {

struct ProductInfo {
    juce::String name;
    preset::Tag stateTag {};
    std::uint32_t stateSchema = 1;
    juce::Colour accent;
};

struct EditorLimits {
    static constexpr int minWidth = 1040, minHeight = 700;
    static constexpr int defaultWidth = 1180, defaultHeight = 760;
    static constexpr int maxWidth = 1500, maxHeight = 920;

    static bool valid (std::uint32_t w, std::uint32_t h) noexcept
    {
        return w >= minWidth && w <= maxWidth && h >= minHeight && h <= maxHeight;
    }
};

// Section tags of state schema 1.
inline constexpr preset::Tag uiSectionTag = preset::makeTag ("UIST");

class SuiteProcessor : public juce::AudioProcessor {
public:
    explicit SuiteProcessor (ProductInfo info)
        : juce::AudioProcessor (BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true)),
          info_ (std::move (info))
    {
    }

    // A product with more than the Main stereo output declares its buses
    // and overrides isBusesLayoutSupported.
    SuiteProcessor (ProductInfo info, const BusesProperties& buses) : juce::AudioProcessor (buses), info_ (std::move (info)) {}

    const ProductInfo& product() const noexcept { return info_; }

    //==========================================================================
    const juce::String getName() const override { return info_.name; }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.inputBuses.isEmpty()
            && layouts.outputBuses.size() == 1
            && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void prepareToPlay (double sampleRate, int maximumBlockSize) override
    {
        preparedSampleRate_ = sampleRate;
        preparedBlockSize_ = maximumBlockSize;
    }

    void releaseResources() override {}

    // Realtime: no allocation, lock, I/O or logging. MIDI is counted (for the
    // tests and future diagnostics) and otherwise has no effect yet.
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;
        noteMidiSeen (midi);
        buffer.clear();
    }

    using juce::AudioProcessor::processBlock;

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    // VST3 hosts and the Steinberg validator require a named program.
    const juce::String getProgramName (int) override { return "Init"; }
    void changeProgramName (int, const juce::String&) override {}

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    //==========================================================================
    // State schema 1: UIST (u32 width, u32 height). Unknown sections are kept
    // and written back unchanged, so a newer minor writer loses nothing.
    //
    // Every saveable field lives in one SaveableState guarded by stateLock_
    // (message/host threads only, never the audio thread). Save copies the
    // whole state in one critical section; restore and editor resize commit
    // in one critical section. A saved document is therefore always one
    // committed state, never a mix of two (review 6724525306, P1).
    //
    // No host notification or listener callback ever runs under stateLock_
    // (review 6733006001, P1). Product parameters are restored in three
    // ordered steps:
    //   1. commit (stateLock_): the UI state and the restored document's
    //      product sections are published together and the restore is
    //      marked in flight;
    //   2. apply (no stateLock_): the product sets its parameters, which
    //      notifies the host and listeners; they may save or read state;
    //   3. settle (stateLock_): the product records what it applied and the
    //      in-flight mark is cleared.
    // A save while a restore is in flight writes the state being restored
    // (the product sections it will write once settled, published in step 1),
    // so it is never a mix of the old and new parameters. Restores are serialised by restoreLock_,
    // which getStateInformation never takes.
    void getStateInformation (juce::MemoryBlock& destination) override
    {
        SaveableState snapshot;
        {
            const juce::ScopedLock lock (stateLock_);
            snapshot = saved_;
            if (restoring_)
                snapshot.product = restoringProduct_;
            else
                writeProductState (snapshot.product);
        }

        preset::StateDocument document;
        document.product = info_.stateTag;
        document.schema = info_.stateSchema;

        preset::Section ui;
        ui.tag = uiSectionTag;
        appendU32 (ui.data, static_cast<std::uint32_t> (snapshot.editorWidth));
        appendU32 (ui.data, static_cast<std::uint32_t> (snapshot.editorHeight));
        document.sections.push_back (std::move (ui));
        for (auto& section : snapshot.product)
            document.sections.push_back (std::move (section));
        const auto ownSections = document.sections.size();
        for (auto& section : snapshot.preserved)
            document.sections.push_back (std::move (section));

        // Never an empty block for a state that can be saved: if the whole
        // document does not fit the container, the product's own state is
        // saved without the sections of unknown writers (and that is
        // reported). Restore keeps those within a budget so this is not
        // reached through a project the plugin itself accepted.
        auto bytes = preset::encode (document);
        if (! bytes && document.sections.size() > ownSections)
        {
            document.sections.resize (ownSections);
            bytes = preset::encode (document);
            const juce::ScopedLock lock (stateLock_);
            lastStateNotice_ = "The project state was too large with the sections of an unknown writer; they were not saved.";
        }
        if (bytes)
            destination.replaceAll (bytes->data(), bytes->size());
        else
        {
            destination.reset();
            const juce::ScopedLock lock (stateLock_);
            lastStateError_ = "the state is too large to save";
        }
    }

    // Transactional: decode and validate everything first; the live state is
    // changed only when the whole blob is accepted.
    void setStateInformation (const void* data, int sizeInBytes) override
    {
        const juce::ScopedLock serialise (restoreLock_);
        const auto bytes = std::span<const std::byte> (static_cast<const std::byte*> (data),
                                                       data != nullptr && sizeInBytes > 0
                                                           ? static_cast<std::size_t> (sizeInBytes) : 0u);
        auto decoded = preset::decode (bytes, info_.stateTag, info_.stateSchema);
        if (! decoded.ok())
        {
            rejectState (juce::String (std::string (preset::describe (decoded.error))));
            return;
        }

        const auto* ui = decoded.document.find (uiSectionTag);
        if (ui == nullptr || ui->data.size() != 8)
        {
            rejectState ("missing or malformed UI section");
            return;
        }
        const auto width = readU32 (ui->data, 0);
        const auto height = readU32 (ui->data, 4);
        if (! EditorLimits::valid (width, height))
        {
            rejectState ("UI size out of range");
            return;
        }

        juce::String productError;
        auto product = readProductState (decoded.document, productError);
        if (! product.apply || ! product.settle)
        {
            rejectState (productError.isNotEmpty() ? productError : juce::String ("malformed product state"));
            return;
        }

        SaveableState next;
        next.editorWidth = static_cast<int> (width);
        next.editorHeight = static_cast<int> (height);
        auto productSections = std::move (product.sections);
        // Sections of an unknown (newer) writer are kept for the next save,
        // within what the container has left after the product's own
        // worst-case state: more would make the project unsaveable later.
        const std::size_t reserve = productStateReserve() + 4096;
        std::size_t budget = preset::maxStateBytes > reserve ? preset::maxStateBytes - reserve : 0;
        std::size_t dropped = 0, droppedBytes = 0;
        for (auto& section : decoded.document.sections)
            if (section.tag != uiSectionTag && ! isProductSection (section.tag))
            {
                const std::size_t cost = section.data.size() + 16;
                if (cost <= budget)
                {
                    budget -= cost;
                    next.preserved.push_back (std::move (section));
                }
                else
                {
                    ++dropped;
                    droppedBytes += section.data.size();
                }
            }
        const juce::String notice = dropped == 0 ? juce::String()
                                                 : juce::String (static_cast<int> (dropped)) + " section(s) of an unknown writer ("
                                                       + juce::String (static_cast<juce::int64> (droppedBytes))
                                                       + " bytes) were not kept: they would leave too little room to save this project.";

        // 1. Commit: the UI state and the restored product sections, in one
        // critical section; previous values are destroyed after it.
        {
            const juce::ScopedLock lock (stateLock_);
            std::swap (saved_, next);
            std::swap (restoringProduct_, productSections);
            restoring_ = true;
            lastStateError_.clear();
            lastStateNotice_ = notice;
        }
        // 2. Apply: host and listener notifications, without stateLock_.
        product.apply();
        // 3. Settle.
        {
            const juce::ScopedLock lock (stateLock_);
            product.settle();
            restoring_ = false;
            std::swap (restoringProduct_, productSections);
        }
    }

    //==========================================================================
    juce::String lastStateError() const
    {
        const juce::ScopedLock lock (stateLock_);
        return lastStateError_;
    }

    // Not an error: what the last restore or save had to leave out ("" if nothing).
    juce::String lastStateNotice() const
    {
        const juce::ScopedLock lock (stateLock_);
        return lastStateNotice_;
    }

    std::uint64_t midiEventsSeen() const noexcept { return midiEventsSeen_.load (std::memory_order_relaxed); }
    double preparedSampleRate() const noexcept { return preparedSampleRate_; }
    int preparedBlockSize() const noexcept { return preparedBlockSize_; }

    // Width and height are read and written as one pair.
    std::pair<int, int> editorSize() const
    {
        const juce::ScopedLock lock (stateLock_);
        return { saved_.editorWidth, saved_.editorHeight };
    }

    int editorWidth() const { return editorSize().first; }
    int editorHeight() const { return editorSize().second; }

    void setEditorSize (int width, int height)
    {
        if (! EditorLimits::valid (static_cast<std::uint32_t> (width), static_cast<std::uint32_t> (height)))
            return;
        const juce::ScopedLock lock (stateLock_);
        saved_.editorWidth = width;
        saved_.editorHeight = height;
    }

protected:
    //==========================================================================
    // Product state hooks (message/host threads, never the audio thread).
    //
    // writeProductState appends the product's sections to a document being
    // saved; it runs under stateLock_ and must not call out (no notification,
    // no listener). readProductState validates the product sections of a
    // decoded document without changing anything (no lock held) and returns
    // the restore steps, or empty steps and an error when the document is
    // rejected: `apply` runs without stateLock_ and may notify the host;
    // `settle` runs under stateLock_ and must not call out. A product section
    // is never kept as an unknown section.
    struct ProductRestore {
        std::function<void()> apply;
        std::function<void()> settle;
        // What writeProductState will write once the restore has settled;
        // saved for the product while the restore is in flight.
        std::vector<preset::Section> sections;
    };

    virtual void writeProductState (std::vector<preset::Section>&) const {}

    virtual ProductRestore readProductState (const preset::StateDocument&, juce::String&)
    {
        return { [] {}, [] {}, {} };
    }

    virtual bool isProductSection (preset::Tag) const { return false; }
    // Upper bound of the product's own sections in bytes (0: no bound known).
    virtual std::size_t productStateReserve() const { return 0; }

    // Realtime safe.
    void noteMidiSeen (const juce::MidiBuffer& midi) noexcept
    {
        midiEventsSeen_.fetch_add (static_cast<std::uint64_t> (midi.getNumEvents()),
                                   std::memory_order_relaxed);
    }

private:
    static void appendU32 (std::vector<std::byte>& out, std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            out.push_back (static_cast<std::byte> ((value >> shift) & 0xffu));
    }

    static std::uint32_t readU32 (const std::vector<std::byte>& in, std::size_t offset) noexcept
    {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 4; ++i)
            value |= std::to_integer<std::uint32_t> (in[offset + i]) << (8 * i);
        return value;
    }

    void rejectState (const juce::String& reason)
    {
        const juce::ScopedLock lock (stateLock_);
        lastStateError_ = reason;
    }

    ProductInfo info_;
    double preparedSampleRate_ = 0.0;
    int preparedBlockSize_ = 0;
    std::atomic<std::uint64_t> midiEventsSeen_ { 0 };
    struct SaveableState {
        int editorWidth = EditorLimits::defaultWidth;
        int editorHeight = EditorLimits::defaultHeight;
        std::vector<preset::Section> product;     // filled only while saving
        std::vector<preset::Section> preserved;
    };

    // Message/host threads only; never taken on the audio thread.
    juce::CriticalSection stateLock_;
    juce::CriticalSection restoreLock_;   // serialises restores; never taken by save
    SaveableState saved_;
    bool restoring_ = false;                          // stateLock_
    std::vector<preset::Section> restoringProduct_;   // stateLock_
    mutable juce::String lastStateError_, lastStateNotice_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuiteProcessor)
};

} // namespace vox::suite

#include "common/SuiteEditor.h"

inline juce::AudioProcessorEditor* vox::suite::SuiteProcessor::createEditor()
{
    return new SuiteEditor (*this);
}

inline vox::suite::SuiteEditor::SuiteEditor (SuiteProcessor& owner)
    : juce::AudioProcessorEditor (owner),
      processor_ (owner),
      productName_ (owner.product().name),
      accent_ (owner.product().accent)
{
    const auto stateError = owner.lastStateError();
    footerText_ = stateError.isEmpty()
                      ? "State schema " + juce::String (owner.product().stateSchema)
                      : "Last project state was rejected: " + stateError;
    setLookAndFeel (&lookAndFeel_);
    setResizable (true, true);
    setResizeLimits (EditorLimits::minWidth, EditorLimits::minHeight,
                     EditorLimits::maxWidth, EditorLimits::maxHeight);
    const auto [width, height] = owner.editorSize();
    setSize (width, height);
}

inline void vox::suite::SuiteEditor::resized()
{
    processor_.setEditorSize (getWidth(), getHeight());
    if (content_ != nullptr)
        content_->setBounds (getLocalBounds().withTrimmedTop (headerHeight).withTrimmedBottom (footerHeight));
}
