#include "SampleLibrary.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace vox::drums {
namespace {

std::atomic<std::uint64_t> nextBufferId { 1 };

void putU32 (std::vector<std::byte>& out, std::uint32_t v)
{
    for (int s = 0; s < 32; s += 8)
        out.push_back (static_cast<std::byte> ((v >> s) & 0xffu));
}

void putU64 (std::vector<std::byte>& out, std::uint64_t v)
{
    putU32 (out, static_cast<std::uint32_t> (v & 0xffffffffu));
    putU32 (out, static_cast<std::uint32_t> (v >> 32));
}

void putString (std::vector<std::byte>& out, const juce::String& text)
{
    const auto utf8 = text.toStdString();
    putU32 (out, static_cast<std::uint32_t> (utf8.size()));
    for (const char c : utf8)
        out.push_back (static_cast<std::byte> (c));
}

struct Reader {
    const std::vector<std::byte>& data;
    std::size_t at = 0;
    bool ok = true;

    bool need (std::size_t n)
    {
        ok = ok && at <= data.size() && data.size() - at >= n;
        return ok;
    }
    std::uint8_t u8()
    {
        if (! need (1))
            return 0;
        return std::to_integer<std::uint8_t> (data[at++]);
    }
    std::uint32_t u32()
    {
        if (! need (4))
            return 0;
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= std::to_integer<std::uint32_t> (data[at + static_cast<std::size_t> (i)]) << (8 * i);
        at += 4;
        return v;
    }
    std::uint64_t u64()
    {
        const auto lo = u32();
        const auto hi = u32();
        return lo | (static_cast<std::uint64_t> (hi) << 32);
    }
    juce::String text (std::uint32_t maxBytes)
    {
        const auto n = u32();
        if (n > maxBytes || ! need (n))
        {
            ok = false;
            return {};
        }
        std::string s (n, '\0');
        for (std::uint32_t i = 0; i < n; ++i)
            s[i] = static_cast<char> (data[at + i]);
        at += n;
        if (! juce::CharPointer_UTF8::isValidString (s.data(), static_cast<int> (s.size())))
        {
            ok = false;
            return {};
        }
        return juce::String::fromUTF8 (s.data(), static_cast<int> (s.size()));
    }
};

juce::AudioFormatManager& formats()
{
    // Message/host threads and the decoder thread; registerBasicFormats is
    // done once, and creating readers is thread safe.
    static juce::AudioFormatManager manager;
    static std::once_flag registered;
    std::call_once (registered, [] { manager.registerBasicFormats(); });
    return manager;
}

juce::String padLabel (int pad)
{
    return "Pad " + juce::String (pad + 1) + " (" + juce::String (DrumEngine::padNames()[static_cast<std::size_t> (pad)]) + ")";
}

} // namespace

juce::String hexOf (const std::array<std::uint8_t, 32>& sha)
{
    return juce::String::toHexString (sha.data(), static_cast<int> (sha.size()), 0);
}

DecodeResult decodeSampleFile (const juce::File& file, std::size_t budgetBytes)
{
    DecodeResult result;
    if (! file.existsAsFile())
    {
        result.error = "file not found: " + file.getFullPathName();
        return result;
    }
    const auto size = file.getSize();
    if (size <= 0)
    {
        result.error = "empty file: " + file.getFileName();
        return result;
    }
    if (size > SampleLimits::maxFileBytes)
    {
        result.error = "file larger than 1 GiB: " + file.getFileName();
        return result;
    }
    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes) || static_cast<std::int64_t> (bytes.getSize()) != size)
    {
        result.error = "cannot read file: " + file.getFileName();
        return result;
    }

    auto stream = std::make_unique<juce::MemoryInputStream> (bytes, false);
    std::unique_ptr<juce::AudioFormatReader> reader (formats().createReaderFor (std::move (stream)));
    if (reader == nullptr)
    {
        result.error = "not a supported audio file (WAV, AIFF, FLAC, Ogg): " + file.getFileName();
        return result;
    }
    const double rate = reader->sampleRate;
    const auto frames = reader->lengthInSamples;
    if (! (rate >= 8000.0) || rate > SampleLimits::maxSampleRate)
    {
        result.error = "unsupported sample rate " + juce::String (rate) + " Hz: " + file.getFileName();
        return result;
    }
    if (reader->numChannels < 1 || frames < 2)
    {
        result.error = "no audio in file: " + file.getFileName();
        return result;
    }
    if (static_cast<double> (frames) > SampleLimits::maxSeconds * rate)
    {
        result.error = "longer than " + juce::String (SampleLimits::maxSeconds, 0) + " s: " + file.getFileName();
        return result;
    }
    const int channels = static_cast<int> (std::min<unsigned int> (reader->numChannels, 2u));
    const auto needed = static_cast<std::size_t> (frames) * static_cast<std::size_t> (channels) * sizeof (float);
    if (needed > budgetBytes)
    {
        result.error = "sample memory budget exceeded (" + juce::String (static_cast<juce::int64> (needed >> 20))
                     + " MiB needed): " + file.getFileName();
        return result;
    }

    auto buffer = std::make_shared<SampleBuffer>();
    buffer->frames = static_cast<std::uint64_t> (frames);
    buffer->sampleRate = rate;
    buffer->data.assign (static_cast<std::size_t> (channels), std::vector<float> (static_cast<std::size_t> (frames)));
    {
        // Read in chunks into the float channels (the reader handles every
        // bit depth and float format).
        juce::AudioBuffer<float> chunk (static_cast<int> (reader->numChannels), 65536);
        for (juce::int64 at = 0; at < frames;)
        {
            const int n = static_cast<int> (std::min<juce::int64> (65536, frames - at));
            if (! reader->read (&chunk, 0, n, at, true, true))
            {
                result.error = "corrupt or truncated audio data: " + file.getFileName();
                return result;
            }
            for (int ch = 0; ch < channels; ++ch)
                std::memcpy (buffer->data[static_cast<std::size_t> (ch)].data() + at, chunk.getReadPointer (ch),
                             static_cast<std::size_t> (n) * sizeof (float));
            at += n;
        }
    }
    float peak = 0.0f;
    for (auto& channel : buffer->data)
        for (auto& x : channel)
        {
            if (! std::isfinite (x))
                x = 0.0f;
            peak = std::max (peak, std::abs (x));
        }
    buffer->peak = peak;
    for (auto& channel : buffer->data)
        buffer->pointers.push_back (channel.data());

    const auto buckets = buffer->overviewMin.size();
    for (std::size_t b = 0; b < buckets; ++b)
    {
        const auto begin = buffer->frames * b / buckets;
        const auto end = std::max (begin + 1, buffer->frames * (b + 1) / buckets);
        float lo = 0.0f, hi = 0.0f;
        for (const auto& channel : buffer->data)
            for (auto i = begin; i < end && i < buffer->frames; ++i)
            {
                lo = std::min (lo, channel[static_cast<std::size_t> (i)]);
                hi = std::max (hi, channel[static_cast<std::size_t> (i)]);
            }
        buffer->overviewMin[b] = lo;
        buffer->overviewMax[b] = hi;
    }

    const juce::SHA256 sha (bytes);
    const auto raw = sha.getRawData();
    std::memcpy (buffer->sha256.data(), raw.getData(), 32);
    buffer->id = nextBufferId.fetch_add (1);
    result.buffer = std::move (buffer);
    return result;
}

//==============================================================================
struct SampleLibrary::Table {
    std::uint64_t generation = 0;
    std::array<SampleView, padCount> views {};
    std::array<std::shared_ptr<const SampleBuffer>, padCount> buffers {};
};

SampleLibrary::SampleLibrary()
{
    const juce::ScopedLock sl (lock_);
    publishLocked();
    startTimerHz (10);
}

SampleLibrary::~SampleLibrary()
{
    stopTimer();
    decoder_.removeAllJobs (true, 30000);
    masterReference.clear();
}

const SampleView* SampleLibrary::acquire() noexcept
{
    inUse_ = current_.load (std::memory_order_acquire);
    return inUse_ != nullptr ? inUse_->views.data() : nullptr;
}

void SampleLibrary::report (const SampleUsage& usage) noexcept
{
    const auto seq = reportSeq_.load (std::memory_order_relaxed);
    reportSeq_.store (seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_release);
    reportGeneration_.store (inUse_ != nullptr ? inUse_->generation : 0, std::memory_order_relaxed);
    reportOverflow_.store (usage.overflow, std::memory_order_relaxed);
    const auto count = std::min (usage.count, maxSampleUsage);
    reportCount_.store (count, std::memory_order_relaxed);
    for (std::uint32_t i = 0; i < count; ++i)
        reportIds_[i].store (usage.ids[i], std::memory_order_relaxed);
    reportSeq_.store (seq + 2, std::memory_order_release);
}

void SampleLibrary::publishLocked()
{
    auto table = std::make_unique<Table>();
    table->generation = ++generation_;
    for (int pad = 0; pad < padCount; ++pad)
    {
        const auto& buffer = pads_[static_cast<std::size_t> (pad)].buffer;
        auto& view = table->views[static_cast<std::size_t> (pad)];
        table->buffers[static_cast<std::size_t> (pad)] = buffer;
        if (buffer != nullptr)
        {
            view.id = buffer->id;
            view.channels = buffer->pointers.data();
            view.channelCount = static_cast<std::uint32_t> (buffer->pointers.size());
            view.frames = buffer->frames;
            view.sampleRate = buffer->sampleRate;
            view.peak = buffer->peak;
        }
    }
    // Buffers of the old table that the new one no longer holds wait for a
    // report that shows no voice holds them.
    if (owned_ != nullptr)
    {
        for (const auto& old : owned_->buffers)
            if (old != nullptr
                && std::none_of (table->buffers.begin(), table->buffers.end(), [&] (const auto& b) { return b == old; }))
                retiredBuffers_.push_back ({ old, table->generation });
    }
    const auto* published = table.get();
    current_.store (published, std::memory_order_release);
    if (owned_ != nullptr)
        retiredTables_.push_back ({ std::move (owned_), table->generation });
    owned_ = std::move (table);
}

std::size_t SampleLibrary::collect()
{
    std::uint64_t generation = 0;
    std::uint32_t count = 0;
    bool overflow = true;
    std::array<std::uint64_t, maxSampleUsage> ids {};
    bool consistent = false;
    for (int attempt = 0; attempt < 8 && ! consistent; ++attempt)
    {
        const auto before = reportSeq_.load (std::memory_order_acquire);
        if (before & 1u)
            continue;
        generation = reportGeneration_.load (std::memory_order_relaxed);
        overflow = reportOverflow_.load (std::memory_order_relaxed);
        count = std::min (reportCount_.load (std::memory_order_relaxed), maxSampleUsage);
        for (std::uint32_t i = 0; i < count; ++i)
            ids[i] = reportIds_[i].load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        consistent = before != 0 && reportSeq_.load (std::memory_order_relaxed) == before;
    }

    std::vector<std::shared_ptr<const SampleBuffer>> toFree;
    std::vector<std::unique_ptr<const Table>> tablesToFree;
    std::size_t waiting = 0;
    {
        const juce::ScopedLock sl (lock_);
        if (consistent)
        {
            for (auto it = retiredTables_.begin(); it != retiredTables_.end();)
            {
                if (generation >= it->until)
                {
                    tablesToFree.push_back (std::move (it->table));
                    it = retiredTables_.erase (it);
                }
                else
                    ++it;
            }
            for (auto it = retiredBuffers_.begin(); it != retiredBuffers_.end();)
            {
                const bool held = std::find (ids.begin(), ids.begin() + count, it->buffer->id) != ids.begin() + count;
                if (generation >= it->until && ! overflow && ! held)
                {
                    toFree.push_back (std::move (it->buffer));
                    it = retiredBuffers_.erase (it);
                }
                else
                    ++it;
            }
        }
        waiting = retiredTables_.size() + retiredBuffers_.size();
    }
    // Freed here, on the message thread, outside the lock.
    tablesToFree.clear();
    toFree.clear();
    return waiting;
}

void SampleLibrary::collectAllStopped()
{
    std::vector<RetiredTable> tables;
    std::vector<RetiredBuffer> buffers;
    {
        const juce::ScopedLock sl (lock_);
        tables.swap (retiredTables_);
        buffers.swap (retiredBuffers_);
    }
}

std::uint64_t SampleLibrary::generation() const
{
    const juce::ScopedLock sl (lock_);
    return generation_;
}

std::size_t SampleLibrary::retiredCount() const
{
    const juce::ScopedLock sl (lock_);
    return retiredTables_.size() + retiredBuffers_.size();
}

SampleLibrary::PadInfo SampleLibrary::pad (int pad) const
{
    const juce::ScopedLock sl (lock_);
    return pad >= 0 && pad < padCount ? pads_[static_cast<std::size_t> (pad)] : PadInfo {};
}

std::size_t SampleLibrary::totalBytes() const
{
    const juce::ScopedLock sl (lock_);
    std::size_t total = 0;
    for (const auto& p : pads_)
        if (p.buffer != nullptr)
            total += p.buffer->bytes();
    return total;
}

void SampleLibrary::assign (int pad, std::shared_ptr<SampleBuffer> buffer, const juce::File& source)
{
    if (pad < 0 || pad >= padCount)
        return;
    const juce::ScopedLock sl (lock_);
    ++requests_[static_cast<std::size_t> (pad)];   // supersedes a pending load
    setPadLocked (pad, std::move (buffer), source);
}

void SampleLibrary::setPadLocked (int pad, std::shared_ptr<SampleBuffer> buffer, const juce::File& source)
{
    auto& info = pads_[static_cast<std::size_t> (pad)];
    if (buffer == nullptr)
    {
        info = {};
    }
    else
    {
        SampleReference ref;
        ref.sha256 = buffer->sha256;
        ref.path = source.getFullPathName();
        ref.name = source.getFileName();
        ref.frames = buffer->frames;
        ref.channels = static_cast<std::uint32_t> (buffer->data.size());
        ref.sampleRate = buffer->sampleRate;
        info.reference = ref;
        info.buffer = std::move (buffer);
        info.status.clear();
    }
    publishLocked();
}

void SampleLibrary::clear (int pad)
{
    assign (pad, nullptr, {});
}

void SampleLibrary::loadAsync (int pad, const juce::File& file, std::function<void (const juce::String&)> done)
{
    std::size_t budget = SampleLimits::maxTotalBytes;
    {
        const juce::ScopedLock sl (lock_);
        std::size_t others = 0;
        for (int i = 0; i < padCount; ++i)
            if (i != pad && pads_[static_cast<std::size_t> (i)].buffer != nullptr)
                others += pads_[static_cast<std::size_t> (i)].buffer->bytes();
        budget = others < budget ? budget - others : 0;
    }
    if (pad < 0 || pad >= padCount)
        return;
    std::uint64_t token = 0;
    std::function<void (std::function<void()>)> dispatch;
    {
        const juce::ScopedLock sl (lock_);
        token = ++requests_[static_cast<std::size_t> (pad)];
        dispatch = dispatch_;
    }
    juce::WeakReference<SampleLibrary> weak (this);
    decoder_.addJob ([weak, pad, file, budget, token, dispatch, done = std::move (done)] {
        auto decoded = decodeSampleFile (file, budget);
        std::function<void()> complete = [weak, pad, file, token, decoded = std::move (decoded), done] () mutable {
            auto* self = weak.get();
            if (self == nullptr)
                return;
            bool current = false;
            {
                const juce::ScopedLock sl (self->lock_);
                current = self->requests_[static_cast<std::size_t> (pad)] == token;
                if (current && decoded.buffer != nullptr)
                    self->setPadLocked (pad, std::move (decoded.buffer), file);
            }
            if (done)
                done (current ? decoded.error : juce::String ("Load of " + file.getFileName() + " superseded by a later action"));
        };
        if (dispatch)
            dispatch (std::move (complete));
        else
            juce::MessageManager::callAsync (std::move (complete));
    });
}

juce::File SampleLibrary::libraryFolder()
{
    // VOX_DRUMS_SAMPLE_LIBRARY overrides the location (tests, portable setups).
    const auto overridden = juce::SystemStats::getEnvironmentVariable ("VOX_DRUMS_SAMPLE_LIBRARY", {});
    if (overridden.isNotEmpty() && juce::File::isAbsolutePath (overridden))
        return juce::File (overridden);
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("ULTIMA VOX").getChildFile ("VOX Drums").getChildFile ("Samples");
}

juce::Result SampleLibrary::copyIntoLibrary (int pad)
{
    const auto info = this->pad (pad);
    if (! info.reference || info.buffer == nullptr)
        return juce::Result::fail ("no loaded sample on this pad");
    const juce::File source (info.reference->path);
    const auto folder = libraryFolder();
    if (! folder.createDirectory())
        return juce::Result::fail ("cannot create " + folder.getFullPathName());
    const auto target = folder.getChildFile (hexOf (info.reference->sha256) + source.getFileExtension());
    if (! target.existsAsFile())
    {
        if (! source.existsAsFile() || ! source.copyFileTo (target))
            return juce::Result::fail ("cannot copy " + source.getFullPathName() + " to " + target.getFullPathName());
    }
    const juce::ScopedLock sl (lock_);
    auto& current = pads_[static_cast<std::size_t> (pad)];
    if (current.reference && current.reference->sha256 == info.reference->sha256)
        current.reference->path = target.getFullPathName();
    return juce::Result::ok();
}

//==============================================================================
// DSMP: u32 version (1), u32 pad count (8), then per pad u8 assigned and, if
// assigned: 32-byte SHA-256, u64 frames, u32 channels, u64 sample-rate bits,
// path, name (u32 length + UTF-8). No sample data.
preset::Section SampleLibrary::writeState() const
{
    const juce::ScopedLock sl (lock_);
    return encodeState (pads_);
}

preset::Section SampleLibrary::encodeState (const std::array<PadInfo, padCount>& pads)
{
    preset::Section section;
    section.tag = sampleSectionTag;
    putU32 (section.data, 1);
    putU32 (section.data, padCount);
    for (const auto& p : pads)
    {
        section.data.push_back (std::byte { p.reference ? std::uint8_t { 1 } : std::uint8_t { 0 } });
        if (! p.reference)
            continue;
        const auto& r = *p.reference;
        for (auto b : r.sha256)
            section.data.push_back (static_cast<std::byte> (b));
        putU64 (section.data, r.frames);
        putU32 (section.data, r.channels);
        std::uint64_t bits = 0;
        std::memcpy (&bits, &r.sampleRate, sizeof bits);
        putU64 (section.data, bits);
        putString (section.data, r.path);
        putString (section.data, r.name);
    }
    return section;
}

bool SampleLibrary::readState (const preset::Section* section, Restore& out, juce::String& error)
{
    out = {};
    if (section == nullptr)
        return true;   // no samples assigned (an older document)
    Reader in { section->data };
    const auto version = in.u32();
    const auto count = in.u32();
    if (! in.ok || version != 1 || count != padCount)
    {
        error = "unsupported sample section";
        return false;
    }
    std::array<std::optional<SampleReference>, padCount> refs;
    for (std::uint32_t pad = 0; pad < count; ++pad)
    {
        const auto assigned = in.u8();
        if (! in.ok || assigned > 1)
        {
            error = "malformed sample section";
            return false;
        }
        if (assigned == 0)
            continue;
        SampleReference r;
        if (! in.need (32))
        {
            error = "malformed sample section";
            return false;
        }
        for (auto& b : r.sha256)
            b = in.u8();
        r.frames = in.u64();
        r.channels = in.u32();
        const auto bits = in.u64();
        std::memcpy (&r.sampleRate, &bits, sizeof bits);
        r.path = in.text (4096);
        r.name = in.text (1024);
        if (! in.ok || r.channels < 1 || r.channels > 2 || ! std::isfinite (r.sampleRate))
        {
            error = "malformed sample section";
            return false;
        }
        refs[pad] = r;
    }
    if (in.at != section->data.size())
    {
        error = "malformed sample section";
        return false;
    }

    // Resolve: the assigned path, then the user library by content hash. A
    // file whose content changed is never used in place of the original.
    std::size_t budget = SampleLimits::maxTotalBytes;
    juce::StringArray diagnostics;
    for (int pad = 0; pad < padCount; ++pad)
    {
        auto& p = out.pads[static_cast<std::size_t> (pad)];
        const auto& ref = refs[static_cast<std::size_t> (pad)];
        if (! ref)
            continue;
        p.reference = ref;
        juce::Array<juce::File> candidates;
        const juce::File assigned (ref->path);
        if (juce::File::isAbsolutePath (ref->path) && assigned.existsAsFile())
            candidates.add (assigned);
        for (const auto& f : libraryFolder().findChildFiles (juce::File::findFiles, false, hexOf (ref->sha256) + ".*"))
            candidates.addIfNotAlreadyThere (f);
        juce::String problem = candidates.isEmpty() ? "not found" : juce::String();
        for (const auto& candidate : candidates)
        {
            auto decoded = decodeSampleFile (candidate, budget);
            if (decoded.buffer == nullptr)
            {
                problem = decoded.error;
                continue;
            }
            if (decoded.buffer->sha256 != ref->sha256)
            {
                problem = "file content changed since it was assigned";
                continue;
            }
            budget -= std::min (budget, decoded.buffer->bytes());
            p.buffer = std::move (decoded.buffer);
            problem.clear();
            break;
        }
        if (p.buffer == nullptr)
        {
            p.status = problem;
            diagnostics.add (padLabel (pad) + ": '" + ref->name + "' " + problem + " (" + ref->path + ", sha256 "
                             + hexOf (ref->sha256).substring (0, 12) + "); the pad plays its synthesis");
        }
    }
    out.diagnostics = diagnostics.joinIntoString ("\n");
    return true;
}

void SampleLibrary::setCompletionDispatcherForTesting (std::function<void (std::function<void()>)> dispatch)
{
    const juce::ScopedLock sl (lock_);
    dispatch_ = std::move (dispatch);
}

void SampleLibrary::waitForPendingDecodes()
{
    while (decoder_.getNumJobs() > 0)
        juce::Thread::sleep (2);
}

void SampleLibrary::apply (Restore restore)
{
    const juce::ScopedLock sl (lock_);
    for (int pad = 0; pad < padCount; ++pad)
    {
        ++requests_[static_cast<std::size_t> (pad)];   // a restore supersedes pending loads
        pads_[static_cast<std::size_t> (pad)] = std::move (restore.pads[static_cast<std::size_t> (pad)]);
    }
    publishLocked();
}

juce::String SampleLibrary::diagnostics() const
{
    const juce::ScopedLock sl (lock_);
    juce::StringArray lines;
    for (int pad = 0; pad < padCount; ++pad)
    {
        const auto& p = pads_[static_cast<std::size_t> (pad)];
        if (p.reference && p.buffer == nullptr)
            lines.add (padLabel (pad) + ": '" + p.reference->name + "' " + p.status);
    }
    return lines.joinIntoString ("\n");
}

} // namespace vox::drums
