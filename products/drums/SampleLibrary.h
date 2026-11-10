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

#include "engine/KitEngine.h"
#include "StateContainer.h"

#include <array>
#include <functional>
#include <tuple>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vox::drums {

// Sample slots: pad * 2 + player (A = 0, B = 1), 16 pads.
inline constexpr int slotCount = kit::sampleSlots;
inline constexpr int slotOf (int pad, int player) noexcept { return pad * kit::samplesPerPad + player; }
using SampleView = kit::Sample;
using SampleUsage = kit::Usage;
inline constexpr std::uint32_t maxSampleUsage = static_cast<std::uint32_t> (std::tuple_size_v<decltype (kit::Usage::ids)>);

inline constexpr preset::Tag sampleSectionTag = preset::makeTag ("DSMP");

// Decode limits: one file at most 60 s at its own rate (and at most
// 192 kHz). Decoded memory is a hard cap of 512 MiB (owner decision, #120):
// every decoded buffer the library holds counts against it at all times:
// the slots' buffers, retired buffers a voice may still read, and decodes
// in flight (reserved before they allocate). A load or restore that does
// not fit is refused or deferred; it never displaces the live state first.
struct SampleLimits {
    static constexpr double maxSeconds = 60.0;
    static constexpr double maxSampleRate = 192000.0;
    static constexpr std::size_t maxTotalBytes = std::size_t (512) * 1024 * 1024;
    static constexpr std::int64_t maxFileBytes = std::int64_t (1) << 30;
};

// One decoded sample: float channels (1 or 2) at the file's native rate.
struct SampleBuffer {
    SampleBuffer() noexcept { live.fetch_add (1); }
    ~SampleBuffer() { live.fetch_sub (1); liveBytes.fetch_sub (static_cast<std::int64_t> (accounted)); }
    SampleBuffer (const SampleBuffer&) = delete;
    SampleBuffer& operator= (const SampleBuffer&) = delete;
    static inline std::atomic<std::int64_t> live { 0 };        // buffers alive in this process
    // Decoded bytes alive in this process and their high-water mark (tests).
    static inline std::atomic<std::int64_t> liveBytes { 0 };
    static inline std::atomic<std::int64_t> peakLiveBytes { 0 };
    static void resetPeakForTesting() noexcept { peakLiveBytes.store (liveBytes.load()); }
    // Called by the decoder once `data` is allocated.
    void account() noexcept
    {
        accounted = data.size() * static_cast<std::size_t> (frames) * sizeof (float);
        const auto now = liveBytes.fetch_add (static_cast<std::int64_t> (accounted)) + static_cast<std::int64_t> (accounted);
        auto high = peakLiveBytes.load();
        while (now > high && ! peakLiveBytes.compare_exchange_weak (high, now)) {}
    }
    std::size_t accounted = 0;

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
    std::size_t reserved = 0;   // bytes `reserve` granted (0 if it was not asked)
};

// Decodes one file (WAV, AIFF, FLAC, Ogg). Deterministic errors for a missing,
// empty, unreadable, corrupt, too long or too large file. `reserve` is asked
// for the decoded size before anything is allocated; false refuses the file
// with a "sample memory budget" error.
DecodeResult decodeSampleFile (const juce::File& file, const std::function<bool (std::size_t)>& reserve);
DecodeResult decodeSampleFile (const juce::File& file, std::size_t budgetBytes = SampleLimits::maxTotalBytes);
// Decoded size of a referenced sample (what a restore will need).
std::size_t decodedBytes (const SampleReference& reference) noexcept;

juce::String hexOf (const std::array<std::uint8_t, 32>& sha);

class SampleLibrary final : private juce::Timer {
public:
    SampleLibrary();
    ~SampleLibrary() override;

    //== audio thread =========================================================
    // The pads' current sample views; valid until the next acquire() of the
    // same thread (the table is retired only after a newer report).
    const SampleView* acquire() noexcept;
    // The engine's usage after the block that used acquire()'s table. Every
    // acquire() is followed by exactly one report() or abandon().
    void report (const SampleUsage& usage) noexcept;
    void abandon() noexcept;

    //== message / host threads ===============================================
    struct PadInfo {
        std::optional<SampleReference> reference;   // empty: no sample assigned
        std::shared_ptr<const SampleBuffer> buffer; // null when missing, deferred or none
        juce::String status;                        // "", or why it is missing
        bool deferred = false;                      // restored but waiting for memory
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
    // Restore resolves every reference first (a buffer the library already
    // holds with the same content, the path, then the user library by
    // hash), decoding synchronously on the calling thread into memory it
    // reserves; a sample that does not fit next to what is held now is
    // deferred (reference kept, "waiting for memory") and loaded once the
    // replaced samples are reclaimed. `apply` then publishes. A Restore
    // dropped without apply releases its reservation.
    preset::Section writeState() const;
    static preset::Section encodeState (const std::array<PadInfo, slotCount>& pads);
    struct Restore {
        Restore() = default;
        ~Restore();
        Restore (const Restore&) = delete;
        Restore& operator= (const Restore&) = delete;
        std::array<PadInfo, slotCount> pads;
        juce::String diagnostics;   // one line per missing, changed or deferred sample
        SampleLibrary* owner = nullptr;
        std::size_t reserved = 0;   // decoded bytes of `pads` not yet held by the library
    };
    bool readState (const preset::Section* section, Restore& out, juce::String& error);
    void apply (Restore& restore);

    // Starts loading deferred restored samples that now fit (message
    // thread; also called by collect() and the timer).
    void resumeDeferred();

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
    std::size_t retiredBytes() const;
    // Every decoded byte counted against the budget: slot buffers (each
    // buffer once), retired buffers and reservations of decodes in flight.
    std::size_t committedBytes() const;
    // The decoded-memory budget (default SampleLimits::maxTotalBytes), a
    // hard cap on committedBytes().
    std::size_t budget() const;
    void setBudgetForTesting (std::size_t bytes);
    // Changes with every published table (editor refresh).
    std::uint64_t generation() const;

private:
    struct Table;
    void timerCallback() override { collect(); }   // collect() also resumes deferred loads
    void publishLocked();

    mutable juce::CriticalSection lock_;
    std::array<PadInfo, slotCount> pads_;
    std::array<std::uint64_t, slotCount> requests_ {};   // per slot: latest action (lock_)
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
    std::atomic<bool> busy_ { false };      // a block is between acquire() and report()
    std::size_t budget_ = SampleLimits::maxTotalBytes;   // lock_
    std::size_t reserved_ = 0;                            // lock_: decodes in flight
    std::array<bool, slotCount> deferredLoading_ {};      // lock_: a deferred slot's load runs
    std::size_t retiredBytesLocked() const;
    std::size_t committedLocked() const;
    bool reserveLocked (std::size_t bytes);
    void release (std::size_t bytes);
    // Resolves a reference to a decoded buffer (path, then library by hash,
    // content-checked) with `reserve`; empty buffer and the problem otherwise.
    DecodeResult resolve (const SampleReference& reference, const std::function<bool (std::size_t)>& reserve);

    juce::ThreadPool decoder_ { 1 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (SampleLibrary)
    JUCE_DECLARE_NON_COPYABLE (SampleLibrary)
};

} // namespace vox::drums
