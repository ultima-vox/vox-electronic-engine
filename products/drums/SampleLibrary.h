#pragma once

// VOX Drums pad samples (DRM-02): decoding, ownership, publication to the
// audio thread, deferred retirement and the project-state references.
//
// Threads:
//  - decode: off the audio thread (a background job for editor loads; the
//    restoring thread for project restores);
//  - assign/clear/state: message or host threads, under the library lock;
//  - audio thread: acquire() the current table at block start, report()
//    the engine's sample usage at block end. Both are wait-free: one atomic
//    load, and a seqlock write of a fixed-size record. The audio thread
//    never allocates, frees, locks or touches a shared_ptr.
//
// Ownership (03_VOX_DRUMS_SPEC §5): every published table is immutable and
// generation-numbered. A replaced table, and a buffer that left the table,
// are retired on the message thread and freed there only when the audio
// thread has reported a completed block that (a) ran on a newer table and
// (b) did not list the buffer as held by a voice. A missing, overflowing or
// stale report frees nothing.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>

#include "engine/DrumEngine.h"
#include "StateContainer.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vox::drums {

inline constexpr preset::Tag sampleSectionTag = preset::makeTag ("DSMP");

// Decode limits. Provisional values (the decoded-memory budget is an open
// owner decision, 03_VOX_DRUMS_SPEC §9): one file at most 60 s at its own
// rate (and at most 192 kHz); all pads together at most 512 MiB decoded.
struct SampleLimits {
    static constexpr double maxSeconds = 60.0;
    static constexpr double maxSampleRate = 192000.0;
    static constexpr std::size_t maxTotalBytes = std::size_t (512) * 1024 * 1024;
    static constexpr std::int64_t maxFileBytes = std::int64_t (1) << 30;
};

// One decoded sample: float channels (1 or 2) at the file's native rate.
struct SampleBuffer {
    SampleBuffer() noexcept { live.fetch_add (1); }
    ~SampleBuffer() { live.fetch_sub (1); }
    SampleBuffer (const SampleBuffer&) = delete;
    SampleBuffer& operator= (const SampleBuffer&) = delete;
    static inline std::atomic<std::int64_t> live { 0 };   // buffers alive in this process

    std::uint64_t id = 0;                    // process-unique, never 0
    std::vector<std::vector<float>> data;    // channels
    std::vector<const float*> pointers;      // into data, for the engine
    std::uint64_t frames = 0;
    double sampleRate = 0.0;
    float peak = 0.0f;
    std::array<std::uint8_t, 32> sha256 {};  // of the file bytes
    std::array<float, 512> overviewMin {}, overviewMax {};   // waveform for the editor

    std::size_t bytes() const noexcept { return data.size() * frames * sizeof (float); }
};

// A pad's sample assignment as the user made it. It survives a missing
// file: the reference is kept and saved back unchanged.
struct SampleReference {
    std::array<std::uint8_t, 32> sha256 {};
    juce::String path;       // absolute path when assigned
    juce::String name;       // file name, for display
    std::uint64_t frames = 0;
    std::uint32_t channels = 0;
    double sampleRate = 0.0;
};

struct DecodeResult {
    std::shared_ptr<SampleBuffer> buffer;
    juce::String error;      // set when buffer is null
};

// Decodes one file (WAV, AIFF, FLAC, Ogg). Deterministic errors for a missing,
// empty, unreadable, corrupt, too long or too large file.
DecodeResult decodeSampleFile (const juce::File& file, std::size_t budgetBytes = SampleLimits::maxTotalBytes);

juce::String hexOf (const std::array<std::uint8_t, 32>& sha);

class SampleLibrary final : private juce::Timer {
public:
    SampleLibrary();
    ~SampleLibrary() override;

    //== audio thread =========================================================
    // The pads' current sample views; valid until the next acquire() of the
    // same thread (the table is retired only after a newer report).
    const SampleView* acquire() noexcept;
    // The engine's usage after the block that used acquire()'s table.
    void report (const SampleUsage& usage) noexcept;

    //== message / host threads ===============================================
    struct PadInfo {
        std::optional<SampleReference> reference;   // empty: no sample assigned
        std::shared_ptr<const SampleBuffer> buffer; // null when missing or none
        juce::String status;                        // "", or why it is missing
    };
    PadInfo pad (int pad) const;

    // Assigns a decoded buffer (or clears with null) and publishes a new table.
    void assign (int pad, std::shared_ptr<SampleBuffer> buffer, const juce::File& source);
    void clear (int pad);

    // Starts decoding `file` off the message thread; `done` runs on the
    // message thread with the error text (empty on success, or a note when
    // a later action superseded the load). Every assign, clear, restore and
    // newer load of the pad invalidates a pending load: its result is
    // committed only if it is still the pad's latest request (review
    // 6734821926).
    void loadAsync (int pad, const juce::File& file, std::function<void (const juce::String&)> done);

    // Tests: deliver load completions through `dispatch` instead of the
    // message loop, and wait until no decode is running.
    void setCompletionDispatcherForTesting (std::function<void (std::function<void()>)> dispatch);
    void waitForPendingDecodes();

    // Copies the pad's file into the user sample library, named by content
    // hash, and points the reference at the copy.
    juce::Result copyIntoLibrary (int pad);
    static juce::File libraryFolder();

    // Project state. Save writes the references (never sample data).
    // Restore resolves every reference first (path, then library by hash),
    // decoding synchronously on the calling thread; `apply` then publishes.
    preset::Section writeState() const;
    static preset::Section encodeState (const std::array<PadInfo, padCount>& pads);
    struct Restore {
        std::array<PadInfo, padCount> pads;
        juce::String diagnostics;   // one line per missing or changed sample
    };
    static bool readState (const preset::Section* section, Restore& out, juce::String& error);
    void apply (Restore restore);

    // Every pad's diagnostics after the last restore or load ("" if none).
    juce::String diagnostics() const;

    // Retirement (message thread; also driven by a timer). Returns the
    // number of buffers and tables still waiting.
    std::size_t collect();
    // Called when the audio thread is known to be stopped (prepare,
    // release, destruction): frees everything retired.
    void collectAllStopped();

    std::size_t totalBytes() const;
    std::size_t retiredCount() const;
    // Changes with every published table (editor refresh).
    std::uint64_t generation() const;

private:
    struct Table;
    void timerCallback() override { collect(); }
    void publishLocked();

    mutable juce::CriticalSection lock_;
    std::array<PadInfo, padCount> pads_;
    std::array<std::uint64_t, padCount> requests_ {};   // per pad: latest action (lock_)
    std::function<void (std::function<void()>)> dispatch_;
    void setPadLocked (int pad, std::shared_ptr<SampleBuffer> buffer, const juce::File& source);
    std::atomic<const Table*> current_ { nullptr };
    std::uint64_t generation_ = 0;
    struct RetiredTable { std::unique_ptr<const Table> table; std::uint64_t until; };
    struct RetiredBuffer { std::shared_ptr<const SampleBuffer> buffer; std::uint64_t until; };
    std::vector<RetiredTable> retiredTables_;
    std::vector<RetiredBuffer> retiredBuffers_;
    std::unique_ptr<const Table> owned_;   // the current table

    // Audio thread -> message thread: the latest usage report, as a
    // seqlock over atomics (odd sequence = being written).
    std::atomic<std::uint32_t> reportSeq_ { 0 };
    std::atomic<std::uint64_t> reportGeneration_ { 0 };
    std::atomic<std::uint32_t> reportCount_ { 0 };
    std::atomic<bool> reportOverflow_ { true };
    std::array<std::atomic<std::uint64_t>, maxSampleUsage> reportIds_ {};
    const Table* inUse_ = nullptr;          // audio thread only

    juce::ThreadPool decoder_ { 1 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (SampleLibrary)
    JUCE_DECLARE_NON_COPYABLE (SampleLibrary)
};

} // namespace vox::drums
