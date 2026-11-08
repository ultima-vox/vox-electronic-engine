#pragma once

// FOUND-01 shell shared by the three ULTIMA VOX MUSIC plugins: a plain JUCE
// AudioProcessor with a stereo main output, MIDI input, a versioned VOXS state
// and a VOX-styled editor. There is no sound engine yet: the processor outputs
// silence. Product DSP, parameters and editors arrive in DRM-01 / SYN-01 /
// FX-01. Header-only so each product compiles it into its own plugin; the
// products share no runtime code or DLL.

#include <juce_audio_processors/juce_audio_processors.h>

#include "StateContainer.h"

#include <atomic>
#include <cstdint>
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
        midiEventsSeen_.fetch_add (static_cast<std::uint64_t> (midi.getNumEvents()),
                                   std::memory_order_relaxed);
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
    void getStateInformation (juce::MemoryBlock& destination) override
    {
        preset::StateDocument document;
        document.product = info_.stateTag;
        document.schema = info_.stateSchema;

        preset::Section ui;
        ui.tag = uiSectionTag;
        appendU32 (ui.data, static_cast<std::uint32_t> (editorWidth_.load()));
        appendU32 (ui.data, static_cast<std::uint32_t> (editorHeight_.load()));
        document.sections.push_back (std::move (ui));

        {
            const juce::ScopedLock lock (stateLock_);
            for (const auto& section : preservedSections_)
                document.sections.push_back (section);
        }

        if (const auto bytes = preset::encode (document))
            destination.replaceAll (bytes->data(), bytes->size());
        else
            destination.reset();
    }

    // Transactional: decode and validate everything first; the live state is
    // changed only when the whole blob is accepted.
    void setStateInformation (const void* data, int sizeInBytes) override
    {
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

        std::vector<preset::Section> preserved;
        for (auto& section : decoded.document.sections)
            if (section.tag != uiSectionTag)
                preserved.push_back (std::move (section));

        // Commit.
        editorWidth_.store (static_cast<int> (width));
        editorHeight_.store (static_cast<int> (height));
        const juce::ScopedLock lock (stateLock_);
        preservedSections_ = std::move (preserved);
        lastStateError_.clear();
    }

    //==========================================================================
    juce::String lastStateError() const
    {
        const juce::ScopedLock lock (stateLock_);
        return lastStateError_;
    }

    std::uint64_t midiEventsSeen() const noexcept { return midiEventsSeen_.load (std::memory_order_relaxed); }
    double preparedSampleRate() const noexcept { return preparedSampleRate_; }
    int preparedBlockSize() const noexcept { return preparedBlockSize_; }

    int editorWidth() const noexcept { return editorWidth_.load(); }
    int editorHeight() const noexcept { return editorHeight_.load(); }

    void setEditorSize (int width, int height) noexcept
    {
        if (EditorLimits::valid (static_cast<std::uint32_t> (width), static_cast<std::uint32_t> (height)))
        {
            editorWidth_.store (width);
            editorHeight_.store (height);
        }
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
    std::atomic<int> editorWidth_ { EditorLimits::defaultWidth };
    std::atomic<int> editorHeight_ { EditorLimits::defaultHeight };

    // Message/host thread only; never taken on the audio thread.
    juce::CriticalSection stateLock_;
    std::vector<preset::Section> preservedSections_;
    juce::String lastStateError_;

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
    setSize (owner.editorWidth(), owner.editorHeight());
}

inline void vox::suite::SuiteEditor::resized()
{
    processor_.setEditorSize (getWidth(), getHeight());
}
