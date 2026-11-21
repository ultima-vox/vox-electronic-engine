#include "Processor.h"
#include "Presets.h"
#include "WorkspaceLibrary.h"
#include <cstdio>
#include <thread>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

using namespace vox::drums;
static_assert (layout::maxOpaqueBytes == vox::preset::maxStateBytes);
namespace {
int failures = 0;
void check (bool value, const char* why)
{
    if (! value) { ++failures; std::printf ("FAIL layout project: %s\n", why); }
}
juce::MemoryBlock state (DrumsProcessor& p) { juce::MemoryBlock b; p.getStateInformation (b); return b; }
vox::preset::StateDocument document (DrumsProcessor& p)
{
    const auto b = state (p);
    return vox::preset::decode ({ static_cast<const std::byte*> (b.getData()), b.getSize() }, p.product().stateTag, 1).document;
}
void restore (DrumsProcessor& p, const vox::preset::StateDocument& doc)
{
    const auto b = vox::preset::encode (doc);
    check (b.has_value(), "fixture fits VOXS");
    if (b) p.setStateInformation (b->data(), static_cast<int> (b->size()));
}
vox::preset::Section section (layout::Bytes b)
{
    vox::preset::Section result { layoutSectionTag, {} };
    for (auto v : b) result.data.push_back (static_cast<std::byte> (v));
    return result;
}
void setLayout (vox::preset::StateDocument& doc, layout::Bytes b)
{
    for (auto& s : doc.sections) if (s.tag == layoutSectionTag) { s = section (std::move (b)); return; }
    doc.sections.push_back (section (std::move (b)));
}
bool sameMusic (const vox::preset::StateDocument& a, const vox::preset::StateDocument& b)
{
    for (const auto& s : a.sections)
    {
        if (s.tag == layoutSectionTag || s.tag == vox::suite::uiSectionTag || s.tag == uiStateSectionTag) continue;
        const auto* other = b.find (s.tag);
        if (other == nullptr || other->data != s.data) return false;
    }
    return true;
}
struct Listener : juce::ChangeListener {
    int calls = 0;
    bool onMessageThread = true;
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        ++calls;
        onMessageThread = onMessageThread && juce::MessageManager::getInstance()->isThisTheMessageThread();
    }
};

void testWorkspaceFiles()
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("vox-workspaces", {}, false);
    folder.createDirectory();
    WorkspaceLibrary library (folder.getChildFile ("workspaces.json"));
    check (library.read().entries.empty() && ! library.file().exists(), "missing user file gives factory-only menu without writes");
    std::vector<WorkspaceLibrary::Entry> entries { { juce::String::fromUTF8 (reinterpret_cast<const char*> (u8"\u0423\u0434\u0430\u0440\u043d\u044b\u0435 \u65e5\u672c\u8a9e \U0001f39b")), layout::factory (layout::Preset::soundDesign) } };
    check (library.write (entries).wasOk(), "Unicode workspace writes atomically");
    const auto read = library.read();
    check (read.diagnostic.isEmpty() && read.entries.size() == 1 && read.entries[0].name == entries[0].name
           && read.entries[0].layout == entries[0].layout, "user workspace name/layout round trip");
    const auto previous = library.file().loadFileAsString();
    auto duplicate = entries;
    duplicate[0].name = "Beat"; duplicate.push_back ({ "bEaT", entries[0].layout });
    check (library.write (duplicate).failed() && library.file().loadFileAsString() == previous, "case-insensitive duplicate rejected without replacing file");
    for (auto name : { juce::String {}, juce::String ("   "), juce::String::repeatedString ("x", 65) })
        check (library.write ({ { name, entries[0].layout } }).failed() && library.file().loadFileAsString() == previous,
               "invalid name never replaces saved workspaces");
    entries.clear();
    for (int n = 0; n < 32; ++n) entries.push_back ({ "Workspace " + juce::String (n), layout::factory (layout::Preset::beatmaking) });
    check (library.write (entries).wasOk() && library.read().entries.size() == 32, "32 workspace entries accepted");
    entries.push_back ({ "Workspace 32", entries[0].layout });
    const auto fullFile = library.file().loadFileAsString();
    check (library.write (entries).failed() && library.file().loadFileAsString() == fullFile, "33rd entry rejected atomically");
    entries.pop_back();
    entries.erase (entries.begin());
    check (library.write (entries).wasOk() && library.read().entries.size() == 31, "Delete rewrites named list without changing any project");
#if JUCE_WINDOWS
    const auto beforeDenied = library.file().loadFileAsString();
    const auto handle = CreateFileW (library.file().getFullPathName().toWideCharPointer(), GENERIC_READ,
                                     FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check (handle != INVALID_HANDLE_VALUE, "Windows deny-delete handle opens");
    if (handle != INVALID_HANDLE_VALUE)
    {
        check (library.write ({}).failed() && library.file().loadFileAsString() == beforeDenied,
               "Windows deny-delete replacement preserves original bytes");
        CloseHandle (handle);
    }
    check (library.file().setReadOnly (true), "Windows read-only fixture applied");
    check (library.write ({}).failed() && library.file().loadFileAsString() == beforeDenied,
           "Windows read-only replacement preserves original bytes");
    library.file().setReadOnly (false);
#endif
    const auto encodedLayout = *layout::encode (entries[0].layout);
    const auto base64 = juce::Base64::toBase64 (encodedLayout.data(), encodedLayout.size());
    const auto item = "{\"name\":\"Beat\",\"layout\":\"" + base64 + "\"}";
    for (const auto& invalid : { juce::String ("{broken"), juce::String ("{\"version\":2,\"workspaces\":[]}"),
             juce::String ("{\"version\":1,\"workspaces\":[{\"name\":\"A\",\"layout\":\"!!!\"}]}"),
             "{\"version\":1,\"workspaces\":[" + item + "," + item + "]}",
             juce::String::repeatedString ("x", static_cast<int> (WorkspaceLibrary::maxFileBytes + 1)) })
    {
        check (library.file().replaceWithText (invalid), "corrupt fixture written");
        const auto corrupt = library.read();
        check (corrupt.entries.empty() && corrupt.diagnostic.isNotEmpty() && ! library.file().exists(),
               "corrupt/oversized/duplicate user file quarantined, no partial entries exposed");
    }
    check (folder.findChildFiles (juce::File::findFiles, false, "workspaces.json*.bad*").size() >= 1,
           "corrupt originals preserved in bad files");
    WorkspaceLibrary blocked (folder.getChildFile ("directory"));
    blocked.file().createDirectory();
    check (blocked.write ({}).failed() && blocked.file().isDirectory(), "non-file target never replaced");
    check (library.write ({}).wasOk() && library.read().entries.empty(), "empty named list remains a valid file");
    folder.deleteRecursively();
}

void testPresetIndependence()
{
    auto p = std::make_unique<DrumsProcessor>();
    p->commitLayout (layout::factory (layout::Preset::mixing));
    for (const auto type : { PresetType::kit, PresetType::pad, PresetType::pattern, PresetType::song })
    {
        const auto preset = p->presets().capture (type, "Layout independence", 0);
        p->commitLayout (layout::factory (layout::Preset::composition));
        const auto layoutBefore = document (*p).find (layoutSectionTag)->data;
        check (p->presets().apply (preset.getData(), preset.getSize(), 0, type).wasOk(), "T20 actual kit/pad/pattern/song preset applies");
        check (document (*p).find (layoutSectionTag)->data == layoutBefore, "T20 musical preset load preserves exact project DLAY");
        p->commitLayout (layout::factory (layout::Preset::soundDesign));
        p->setEditorSize (1300, 800);
        const auto afterEdit = document (*p).find (layoutSectionTag)->data;
        check (p->presets().undoLoad().wasOk() && document (*p).find (layoutSectionTag)->data == afterEdit
               && p->editorSize() == std::pair { 1300, 800 }, "Undo Load undoes music without reverting later layout/size edits");
    }
}

void testStateBudget()
{
    // T21: real encoders at their largest fixed dimensions. The optional
    // embedded style is accounted at its full accepted text upper bound;
    // no synthetic invalid style is presented as a restorable fixture.
    auto p = std::make_unique<DrumsProcessor>();
    auto doc = document (*p);
    const auto replace = [&] (vox::preset::Section section) {
        for (auto& old : doc.sections) if (old.tag == section.tag) { old = std::move (section); return; }
        doc.sections.push_back (std::move (section));
    };
    auto slots = std::make_unique<std::array<StoredPattern, PatternStore::slotCount>>();
    auto alternate = std::make_unique<std::array<StoredPattern, PatternStore::slotCount>>();
    for (auto* bank : { slots.get(), alternate.get() })
        for (auto& pattern : *bank)
        {
            pattern.bars = 4; pattern.resolution = timing::Resolution::sixtyFourth;
            for (auto& cell : pattern.cells) { cell.on = true; cell.velocity = 127; }
            check (pattern.valid(), "T21 maximum 4-bar, 64-cell pattern is valid");
        }
    const auto sequence = PatternStore::encode (*slots, *alternate);
    juce::String error;
    auto decodedSlots = std::make_unique<std::array<StoredPattern, PatternStore::slotCount>>();
    auto decodedAlternate = std::make_unique<std::array<StoredPattern, PatternStore::slotCount>>();
    check (PatternStore::decode (sequence, *decodedSlots, *decodedAlternate, error), "T21 maximum DSEQ fixture validates with production decoder");
    replace (sequence);
    auto song = SongData::initial(); song.length = 64; song.loopLength = 64;
    for (auto& bar : song.bars)
    {
        bar.kind = SongBar::Kind::unique;
        bar.cells = songedit::patternToBar ((*slots)[0]);
    }
    const auto songSection = encodeSong (song);
    SongData decodedSong;
    check (decodeSong (songSection, decodedSong, error), "T21 all 64 unique 64-cell Song bars validate");
    replace (songSection);
    std::array<std::optional<SampleReference>, slotCount> refs;
    for (auto& ref : refs)
    {
        SampleReference sample;
        sample.path = "/" + juce::String::repeatedString ("x", 4095);
        sample.name = juce::String::repeatedString ("n", 1024);
        sample.frames = 1; sample.channels = 1; sample.sampleRate = 48000;
        ref = std::move (sample);
    }
    const auto samples = SampleLibrary::encodeReferences (refs);
    decltype (refs) decodedRefs;
    check (SampleLibrary::decodeReferences (samples, decodedRefs, error), "T21 all 32 maximum-path/name sample references validate");
    replace (samples);
    PadRoles roles; roles.explicitRoles = true;
    replace (encodeRoles (roles));
    layout::Document maximum;
    for (int type = 100; type < 107; ++type) maximum.records.push_back ({ static_cast<layout::Byte> (type), layout::Bytes (256, 0xa5) });
    maximum.records.push_back ({ 107, layout::Bytes (226, 0x5a) });
    const auto bytes = layout::encode (maximum);
    check (bytes && bytes->size() == 2048, "T21 DLAY uses its full payload bound");
    setLayout (doc, *bytes);
    const auto encoded = vox::preset::encode (doc);
    // DSTL: u32 version, SHA256[32], u32 length, text; VOXS section header.
    const auto upperBound = encoded->size() + 40 + style::maxTextBytes + 8;
    std::printf ("T21 state budget: fixed fixtures %zu + maximum DSTL %zu = %zu / %zu bytes (UIST/container included)\n",
                 encoded->size(), 40 + style::maxTextBytes + 8, upperBound, DrumsProcessor::productStateBudgetBytes);
    check (upperBound <= DrumsProcessor::productStateBudgetBytes, "T21 largest fixed state + full DSTL upper bound + DLAY/DROL fits unchanged reserve");
}

void testCorruptLayoutBudgetRecovery()
{
    auto p = std::make_unique<DrumsProcessor>();
    auto fixture = document (*p);
    layout::Bytes corrupt (900000, 0); corrupt[0] = 1;
    setLayout (fixture, corrupt);
    fixture.sections.push_back ({ vox::preset::makeTag ("TLOP"), { std::byte { 17 }, std::byte { 93 } } });
    const auto input = vox::preset::encode (fixture);
    check (input && input->size() > 968000 && input->size() < 970000, "TL reported ~968095-byte CRC-valid fixture fits");
    restore (*p, fixture);
    check (p->lastStateError().isEmpty() && ! p->layoutState().recovered && state (*p).getSize() == input->size()
           && document (*p).find (layoutSectionTag)->data == section (corrupt).data,
           "TL invalid v1 stays byte-exact without budget pressure");
    const auto unchanged = state (*p);
    check (input && unchanged == juce::MemoryBlock (input->data(), input->size()),
           "TL no-pressure corrupt-v1 control round-trips the entire VOXS byte-exactly");
    const auto generation = p->layoutState().generation;
    auto badCrc = *input; badCrc.back() ^= std::byte { 1 };
    p->setStateInformation (badCrc.data(), static_cast<int> (badCrc.size()));
    check (p->lastStateError().isNotEmpty() && state (*p) == unchanged && ! p->layoutState().recovered
           && p->layoutState().generation == generation, "TL bad CRC never salvages music or publishes recovery");
    auto badMusic = fixture;
    for (auto& s : badMusic.sections) if (s.tag == padSectionTag) s.data.clear();
    restore (*p, badMusic);
    check (p->lastStateError().isNotEmpty() && state (*p) == unchanged && ! p->layoutState().recovered
           && p->layoutState().generation == generation, "TL invalid music refuses transactionally beside corrupt layout");
    restore (*p, fixture);
    auto all = std::make_unique<PatternStore::Everything>(); p->patterns().copyEverything (*all);
    for (auto* bank : { &all->slots, &all->alternates })
        for (auto& pattern : *bank) { pattern.bars = 4; pattern.resolution = timing::Resolution::sixtyFourth; }
    p->patterns().setEverything (*all);
    auto expected = fixture;
    for (auto& part : expected.sections) if (part.tag == sequenceSectionTag) part = p->patterns().encodeCurrent();
    const auto saved = state (*p);
    std::printf ("TL 7048135242: input %zu, invalid v1 DLAY %zu, save after legal maximum grids %zu / %zu bytes\n",
                 input->size(), corrupt.size(), saved.getSize(), vox::preset::maxStateBytes);
    check (saved.getSize() > 0 && saved.getSize() <= vox::preset::maxStateBytes,
           "TL corrupt v1 cannot prevent saving after legal maximum pattern growth");
    if (saved.getSize() > 0)
    {
        check (p->lastStateError().isEmpty() && p->layoutState().recovered,
               "TL recovery is diagnosed persistently, not presented as lossless restore or a rejected project");
        const auto recoveredGeneration = p->layoutState().generation;
        auto q = std::make_unique<DrumsProcessor>(); q->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        const auto reopened = document (*q);
        check (q->lastStateError().isEmpty() && sameMusic (expected, reopened)
               && reopened.find (vox::suite::uiSectionTag)->data == fixture.find (vox::suite::uiSectionTag)->data,
               "TL recovery reopens all musical/other opaque bytes and UIST untouched");
        check (state (*p) == saved && state (*q) == saved && q->layoutState().recovered
               && p->layoutState().generation == recoveredGeneration, "TL repeated save/reopen preserves diagnostic and never discards again");
        check (p->editSong ([] (SongData& song) { song.length = 32; }), "TL musical edit creates real Undo entry after recovery");
        const auto edited = state (*p);
        p->patternHistory().undo (p->patterns());
        check (state (*p) == saved && p->layoutState().recovered, "TL Undo preserves recovered layout/diagnostic and restores musical bytes");
        p->patternHistory().redo (p->patterns());
        check (state (*p) == edited && p->layoutState().recovered, "TL Redo preserves recovered layout/diagnostic");
        p->commitLayout (layout::factory (layout::Preset::mixing));
        check (p->layoutState().recovered, "TL ordinary layout edits cannot silently erase the recovery diagnostic");
        p->commitLayout (layout::factory (layout::Preset::beatmaking), true);
        check (! p->layoutState().recovered, "TL explicit Reset acknowledges recovery");
    }
}

void testPresentationSize()
{
    auto p = std::make_unique<DrumsProcessor>();
    p->setEditorSize (1600, 1000);
    const auto before = state (*p);
    vox::suite::SuiteEditor frame (*p);
    check (p->editorSize() == std::pair { 1600, 1000 } && frame.getWidth() == 1600 && frame.getHeight() == 1000,
           "T15 constructor constraint setup preserves requested UIST before content exists");
    frame.setPresentationSize (1366, 768);
    frame.setContent (std::make_unique<juce::Component>());
    check (frame.getWidth() == 1366 && frame.getHeight() == 768 && p->editorSize() == std::pair { 1600, 1000 }
           && state (*p) == before, "T15 presentation clamp and content layout never rewrite canonical UIST");
    frame.setSize (1200, 800);
    frame.setContent (std::make_unique<juce::Component>());
    check (p->editorSize() == std::pair { 1200, 800 }, "T15 an actual user resize clears presentation override and persists the new size");
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    testWorkspaceFiles();
    testPresetIndependence();
    testStateBudget();
    testPresentationSize();
    testCorruptLayoutBudgetRecovery();
    auto p = std::make_unique<DrumsProcessor>();
    const auto initial = document (*p);
    check (initial.find (layoutSectionTag) && ! initial.find (uiStateSectionTag), "one canonical DLAY, no DUIS writer");
    p->editSong ([] (SongData& s) { s.length = 32; });
    const auto musical = document (*p);
    const auto undoCount = p->patternHistory().size();
    check (undoCount > 0 && p->commitLayout (layout::factory (layout::Preset::composition)), "layout commit beside real musical undo");
    p->setUiState ({ 7, 15, 31, 3, 1 });
    const auto saved = state (*p);
    check (sameMusic (musical, document (*p)) && p->patternHistory().size() == undoCount, "layout/selection do not modify musical sections or undo");
    auto q = std::make_unique<DrumsProcessor>();
    q->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    check (q->lastStateError().isEmpty() && state (*q) == saved && q->uiState() == UiState { 0, 15, 31, 3, 0 },
           "closed-editor canonical selection restores, obsolete page/export scope do not persist");
    const auto good = document (*p);
    for (const auto& bad : { layout::Bytes {}, layout::Bytes { 1, 0 }, layout::Bytes (2049, 0) })
    {
        auto doc = good;
        setLayout (doc, bad);
        restore (*q, doc);
        const auto snapshot = q->layoutState();
        check (q->lastStateError().isEmpty() && snapshot.status == layout::Status::invalid
               && snapshot.document == layout::factory (layout::Preset::beatmaking), "invalid DLAY restores default and diagnostic status");
        check (document (*q).find (layoutSectionTag)->data == section (bad).data && sameMusic (good, document (*q)),
               "invalid bytes round trip without sacrificing music");
        q->commitLayout (layout::factory (layout::Preset::mixing));
        check (q->layoutState().status == layout::Status::valid, "first committed edit replaces invalid DLAY");
    }
    auto futureDoc = good;
    const layout::Bytes future { 2, 0, 0, 0, 91, 92, 93 };
    setLayout (futureDoc, future);
    restore (*q, futureDoc);
    q->commitLayout (layout::factory (layout::Preset::soundDesign));
    q->setUiState ({ 0, 9, 4, 1, 0 });
    check (q->layoutState().status == layout::Status::future && document (*q).find (layoutSectionTag)->data == section (future).data,
           "future data remains exact after session layout and selection edits");
    q->commitLayout (layout::factory (layout::Preset::beatmaking), true);
    check (q->layoutState().status == layout::Status::valid && document (*q).find (layoutSectionTag)->data != section (future).data,
           "confirmed Reset replaces future layout");
    auto largerFuture = good;
    layout::Bytes futurePayload (8192, 0); futurePayload[0] = 2;
    setLayout (largerFuture, futurePayload); restore (*q, largerFuture);
    q->commitLayout (layout::factory (layout::Preset::composition));
    check (q->lastStateError().isEmpty() && q->layoutState().status == layout::Status::future
           && document (*q).find (layoutSectionTag)->data == section (futurePayload).data,
           "accepted future layout over v1 size remains exact after session edits");
    auto grownPatterns = std::make_unique<PatternStore::Everything>(); q->patterns().copyEverything (*grownPatterns);
    for (auto* bank : { &grownPatterns->slots, &grownPatterns->alternates })
        for (auto& pattern : *bank) { pattern.bars = 4; pattern.resolution = timing::Resolution::sixtyFourth; }
    q->patterns().setEverything (*grownPatterns);
    check (state (*q).getSize() > 0 && document (*q).find (layoutSectionTag)->data == section (futurePayload).data,
           "accepted opaque future data remains saveable and exact after maximum pattern-grid growth");
    const auto safeFuture = state (*q);
    const auto safeGeneration = q->layoutState().generation;
    auto excessiveFuture = good;
    layout::Bytes excessivePayload (900000, 0); excessivePayload[0] = 2;
    setLayout (excessiveFuture, excessivePayload); restore (*q, excessiveFuture);
    check (q->lastStateError().isNotEmpty() && state (*q) == safeFuture && q->layoutState().generation == safeGeneration,
           "future layout that cannot coexist with bounded musical growth is refused transactionally, never silently discarded");
    auto dev = good;
    std::erase_if (dev.sections, [] (const auto& s) { return s.tag == layoutSectionTag; });
    dev.sections.push_back ({ uiStateSectionTag, { std::byte { 255 } } });
    restore (*q, dev);
    check (q->lastStateError().isEmpty() && q->layoutState().document == layout::factory (layout::Preset::beatmaking)
           && q->uiState() == UiState {} && ! document (*q).find (uiStateSectionTag), "T22 development DUIS discarded without migration even when malformed");
    auto corruptMusic = good;
    for (auto& s : corruptMusic.sections) if (s.tag == padSectionTag) s.data.clear();
    setLayout (corruptMusic, *layout::encode (layout::factory (layout::Preset::mixing)));
    const auto before = state (*q);
    const auto generation = q->layoutState().generation;
    restore (*q, corruptMusic);
    check (q->lastStateError().isNotEmpty() && state (*q) == before && q->layoutState().generation == generation,
           "malformed audio section rejects the whole restore, layout included");
    Listener listener;
    q->layoutChanges().addChangeListener (&listener);
    std::thread host ([&] { restore (*q, good); });
    host.join();
    check (listener.calls == 0, "host restore does not touch UI/listeners synchronously");
    juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    check (listener.calls > 0 && listener.onMessageThread, "restore notification delivered on the message thread");
    q->commitLayout (layout::factory (layout::Preset::mixing));
    q->layoutChanges().removeChangeListener (&listener);
    const auto calls = listener.calls;
    juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    check (listener.calls == calls, "closing listener while notification is pending is safe");
    std::printf ("DLAY project: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
