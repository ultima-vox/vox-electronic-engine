#include "Presets.h"

#include "FactoryContent.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>

namespace vox::drums {
namespace {

constexpr std::uint32_t presetTypeVersion = 1;

void putU32 (std::vector<std::byte>& out, std::uint32_t v)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back (static_cast<std::byte> ((v >> shift) & 0xffu));
}

// DPTY: u32 version (1), u8 type, u8 source, u32 name length, UTF-8 name.
preset::Section encodeInfo (const PresetInfo& info)
{
    preset::Section s;
    s.tag = presetTypeSectionTag;
    putU32 (s.data, presetTypeVersion);
    s.data.push_back (static_cast<std::byte> (info.type));
    s.data.push_back (static_cast<std::byte> (std::clamp (info.source, 0, 255)));
    const auto name = info.name.substring (0, 64).toStdString();
    putU32 (s.data, static_cast<std::uint32_t> (name.size()));
    for (const char c : name)
        s.data.push_back (static_cast<std::byte> (c));
    return s;
}

bool decodeInfo (const preset::Section& s, PresetInfo& out)
{
    const auto& d = s.data;
    const auto u32 = [&] (std::size_t at) {
        std::uint32_t v = 0;
        for (std::size_t i = 0; i < 4; ++i)
            v |= std::to_integer<std::uint32_t> (d[at + i]) << (8 * i);
        return v;
    };
    if (d.size() < 10 || u32 (0) != presetTypeVersion)
        return false;
    const auto type = std::to_integer<std::uint8_t> (d[4]);
    const auto length = u32 (6);
    if (type > 3 || length > 512 || d.size() != 10 + length)
        return false;
    out.type = static_cast<PresetType> (type);
    out.source = std::to_integer<std::uint8_t> (d[5]);
    out.name = juce::String::fromUTF8 (reinterpret_cast<const char*> (d.data()) + 10, static_cast<int> (length));
    return true;
}

// Host parameters a kit owns: pad sound and mix, kit globals, the returns.
bool kitScope (const std::string& id, int pad = -1)
{
    for (const auto& entry : pluginParameters())
    {
        if (entry.id != id)
            continue;
        switch (entry.field)
        {
            case Field::level:
            case Field::tune:
            case Field::decay:
            case Field::pan:
            case Field::cutoff:
            case Field::send1:
            case Field::send2:
                return pad < 0 || entry.pad == pad;
            case Field::kitVolume:
            case Field::kitVelocity:
            case Field::kitChoke:
            case Field::delayTime:
            case Field::delayFeedback:
            case Field::delayTone:
            case Field::delayLevel:
            case Field::reverbSize:
            case Field::reverbDamp:
            case Field::reverbLevel:
                return pad < 0;
            case Field::mute:
            case Field::solo:
            case Field::seqMode:
            case Field::seqPattern:
            case Field::seqSwing:
            case Field::seqAccent:
            case Field::seqLane:
                return false;   // performance state, not part of a kit
        }
    }
    return false;
}

preset::Section* findSection (preset::StateDocument& doc, preset::Tag tag)
{
    for (auto& s : doc.sections)
        if (s.tag == tag)
            return &s;
    return nullptr;
}

void replaceSection (preset::StateDocument& doc, preset::Section section)
{
    if (auto* existing = findSection (doc, section.tag))
        *existing = std::move (section);
    else
        doc.sections.push_back (std::move (section));
}

void removeSection (preset::StateDocument& doc, preset::Tag tag)
{
    doc.sections.erase (std::remove_if (doc.sections.begin(), doc.sections.end(), [tag] (const preset::Section& s) { return s.tag == tag; }),
                        doc.sections.end());
}

bool decodeBlob (const void* data, std::size_t size, const DrumsProcessor& processor, preset::StateDocument& out, juce::String& error)
{
    auto decoded = preset::decode ({ static_cast<const std::byte*> (data), data != nullptr ? size : 0u }, processor.product().stateTag,
                                   processor.product().stateSchema);
    if (! decoded.ok())
    {
        error = juce::String (std::string (preset::describe (decoded.error)));
        return false;
    }
    out = std::move (decoded.document);
    return true;
}

juce::MemoryBlock encodeBlob (const preset::StateDocument& doc)
{
    juce::MemoryBlock block;
    if (const auto bytes = preset::encode (doc))
        block.replaceAll (bytes->data(), bytes->size());
    return block;
}

// The current project state, or nothing when it cannot be read back.
std::optional<preset::StateDocument> currentDocument (DrumsProcessor& processor)
{
    juce::MemoryBlock block;
    processor.getStateInformation (block);
    preset::StateDocument doc;
    juce::String error;
    if (block.getSize() == 0 || ! decodeBlob (block.getData(), block.getSize(), processor, doc, error))
        return std::nullopt;
    return doc;
}

const juce::StringArray sampleExtensions { ".wav", ".aif", ".aiff", ".flac", ".ogg" };

} // namespace

const char* presetTypeName (PresetType type) noexcept
{
    static const char* names[4] = { "Kit", "Pad", "Pattern", "Song" };
    return names[static_cast<int> (type) & 3];
}

const char* presetExtension (PresetType type) noexcept
{
    static const char* extensions[4] = { ".voxkitpreset", ".voxpad", ".voxpattern", ".voxsong" };
    return extensions[static_cast<int> (type) & 3];
}

juce::File Presets::userFolder (PresetType type)
{
    const auto overridden = juce::SystemStats::getEnvironmentVariable ("VOX_DRUMS_PRESETS", {});
    const auto root = overridden.isNotEmpty() && juce::File::isAbsolutePath (overridden)
                          ? juce::File (overridden)
                          : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                .getChildFile ("ULTIMA VOX").getChildFile ("VOX Drums").getChildFile ("Presets");
    static const char* folders[4] = { "Kits", "Pads", "Patterns", "Songs" };
    return root.getChildFile (folders[static_cast<int> (type) & 3]);
}

juce::Array<juce::File> Presets::userPresets (PresetType type) const
{
    auto files = userFolder (type).findChildFiles (juce::File::findFiles, false, juce::String ("*") + presetExtension (type));
    files.sort();
    return files;
}

bool Presets::describe (const void* data, std::size_t size, const DrumsProcessor& processor, PresetInfo& out)
{
    preset::StateDocument doc;
    juce::String error;
    if (! decodeBlob (data, size, processor, doc, error))
        return false;
    const auto* info = doc.find (presetTypeSectionTag);
    return info != nullptr && decodeInfo (*info, out);
}

juce::MemoryBlock Presets::capture (PresetType type, const juce::String& name, int index) const
{
    const auto read = currentDocument (processor_);
    if (! read)
        return {};   // never a preset made from a state that could not be read
    const auto& current = *read;
    preset::StateDocument doc;
    doc.product = processor_.product().stateTag;
    doc.schema = processor_.product().stateSchema;
    PresetInfo info { type, name, type == PresetType::pad ? std::clamp (index, 0, padCount - 1) : 0 };
    doc.sections.push_back (encodeInfo (info));
    const auto copy = [&] (preset::Tag tag) {
        if (const auto* s = current.find (tag))
            doc.sections.push_back (*s);
    };
    if (type == PresetType::kit || type == PresetType::pad)
    {
        copy (padSectionTag);
        copy (sampleSectionTag);
        // Roles only when the user set them (the state holds DROL only then).
        // A pad preset carries just its own pad's role.
        if (const auto* s = current.find (rolesSectionTag))
        {
            PadRoles roles;
            juce::String error;
            if (decodeRoles (*s, roles, error) && roles.explicitRoles)
            {
                if (type == PresetType::pad)
                {
                    const auto own = roles.role[static_cast<std::size_t> (info.source)];
                    roles.role.fill (PadRole::none);
                    roles.role[static_cast<std::size_t> (info.source)] = own;
                }
                doc.sections.push_back (encodeRoles (roles));
            }
        }
        std::map<std::string, float> values, kept;
        juce::String error;
        if (const auto* s = current.find (parameterSectionTag))
            decodeParameterValues (*s, values, error);
        for (const auto& [id, value] : values)
            if (kitScope (id, type == PresetType::pad ? info.source : -1))
                kept[id] = value;
        doc.sections.push_back (encodeParameterValues (kept));
    }
    else if (type == PresetType::pattern)
    {
        const int slot = std::clamp (index, 0, PatternStore::slotCount - 1);
        const auto slots = std::make_unique<std::array<StoredPattern, PatternStore::slotCount>>();
        const auto alternates = std::make_unique<std::array<StoredPattern, PatternStore::slotCount>>();
        PatternStore::factorySlots (*slots);
        *alternates = *slots;
        processor_.patterns().copySlot (slot, (*slots)[0]);
        processor_.patterns().copyAlternate (slot, (*alternates)[0]);
        doc.sections.push_back (PatternStore::encode (*slots, *alternates));
    }
    else
    {
        copy (sequenceSectionTag);
        copy (songSectionTag);
        copy (generatorSectionTag);
    }
    if (! complete (doc, type))
        return {};   // an incomplete preset is not written
    return encodeBlob (doc);
}

bool Presets::complete (const preset::StateDocument& document, PresetType type)
{
    const auto has = [&] (preset::Tag tag) {
        const auto* s = document.find (tag);
        return s != nullptr && ! s->data.empty();
    };
    switch (type)
    {
        case PresetType::kit:
        case PresetType::pad:
        {
            std::map<std::string, float> values;
            juce::String error;
            const auto* parameters = document.find (parameterSectionTag);
            return has (padSectionTag) && parameters != nullptr && decodeParameterValues (*parameters, values, error) && ! values.empty();
        }
        case PresetType::pattern: return has (sequenceSectionTag);
        case PresetType::song: return has (sequenceSectionTag) && has (songSectionTag);
    }
    return false;
}

bool Presets::canUndoLoad() const noexcept
{
    return previous_.getSize() > 0 && processor_.patternEpoch() == previousEpoch_;
}

juce::Result Presets::applyDocument (preset::StateDocument&& preset, int target, std::optional<PresetType> expected)
{
    PresetInfo info;
    const auto* infoSection = preset.find (presetTypeSectionTag);
    if (infoSection == nullptr || ! decodeInfo (*infoSection, info))
        return juce::Result::fail ("not a VOX Drums preset");
    if (expected && info.type != *expected)
        return juce::Result::fail (juce::String ("this file is a ") + presetTypeName (info.type) + " preset, not a "
                                   + presetTypeName (*expected) + " preset");
    if (! complete (preset, info.type))
        return juce::Result::fail ("the preset is incomplete");

    juce::MemoryBlock before;
    processor_.getStateInformation (before);
    preset::StateDocument doc;
    juce::String error;
    if (! decodeBlob (before.getData(), before.getSize(), processor_, doc, error))
        return juce::Result::fail ("cannot read the current state: " + error);

    const auto require = [&] (preset::Tag tag) -> const preset::Section* { return preset.find (tag); };
    // Pad roles: the preset's (strict) and the project's current ones.
    PadRoles incomingRoles, currentRoles;
    const auto* incomingRoleSection = require (rolesSectionTag);
    if (incomingRoleSection != nullptr && ! decodeRoles (*incomingRoleSection, incomingRoles, error))
        return juce::Result::fail (error);
    if (const auto* s = doc.find (rolesSectionTag))
        if (! decodeRoles (*s, currentRoles, error))
            return juce::Result::fail (error);
    juce::String notice;
    std::optional<RoleOffer> offer;
    if (info.type == PresetType::kit || info.type == PresetType::pad)
    {
        const auto* pads = require (padSectionTag);
        const auto* params = require (parameterSectionTag);
        if (pads == nullptr || params == nullptr)
            return juce::Result::fail ("the preset is incomplete");
        std::map<std::string, float> incoming, values;
        if (! decodeParameterValues (*params, incoming, error))
            return juce::Result::fail (error);
        if (const auto* s = doc.find (parameterSectionTag))
            if (! decodeParameterValues (*s, values, error))
                return juce::Result::fail (error);
        std::array<std::optional<SampleReference>, slotCount> incomingRefs, refs;
        if (const auto* s = require (sampleSectionTag))
            if (! SampleLibrary::decodeReferences (*s, incomingRefs, error))
                return juce::Result::fail (error);
        if (info.type == PresetType::kit)
        {
            replaceSection (doc, *pads);
            // A kit replaces the roles: the preset's, or the default layout
            // for a kit without roles (legacy and factory kits are that layout).
            if (incomingRoles.explicitRoles)
                replaceSection (doc, encodeRoles (incomingRoles));
            else
            {
                removeSection (doc, rolesSectionTag);
                if (currentRoles.explicitRoles && currentRoles.role != PadRoles::identity())
                    notice = "Roles set to the default layout of this kit";
            }
            for (const auto& [id, value] : incoming)
                if (kitScope (id))
                    values[id] = value;
            refs = incomingRefs;
        }
        else
        {
            const int source = std::clamp (info.source, 0, padCount - 1);
            const int to = target >= 0 && target < padCount ? target : source;
            auto kit = std::make_unique<kit::KitSettings> (defaultKit()), incomingKit = std::make_unique<kit::KitSettings> (defaultKit());
            if (! KitStore::decode (*pads, *incomingKit, error))
                return juce::Result::fail (error);
            if (const auto* s = doc.find (padSectionTag))
                if (! KitStore::decode (*s, *kit, error))
                    return juce::Result::fail (error);
            const auto note = kit->pads[static_cast<std::size_t> (to)].note;
            const auto output = kit->pads[static_cast<std::size_t> (to)].output;
            kit->pads[static_cast<std::size_t> (to)] = incomingKit->pads[static_cast<std::size_t> (source)];
            kit->pads[static_cast<std::size_t> (to)].note = note;       // the target pad keeps its MIDI note
            kit->pads[static_cast<std::size_t> (to)].output = output;   // and its output routing
            replaceSection (doc, KitStore::encode (*kit));
            const auto from = padPrefix (source), dest = padPrefix (to);
            for (const auto& [id, value] : incoming)
                if (kitScope (id, source))
                    values[dest + id.substr (from.size())] = value;
            if (const auto* s = doc.find (sampleSectionTag))
                if (! SampleLibrary::decodeReferences (*s, refs, error))
                    return juce::Result::fail (error);
            for (int player = 0; player < kit::samplesPerPad; ++player)
                refs[static_cast<std::size_t> (slotOf (to, player))] = incomingRefs[static_cast<std::size_t> (slotOf (source, player))];
            // The target keeps its role; a different preset role is only offered.
            if (incomingRoles.explicitRoles)
            {
                const auto offered = incomingRoles.role[static_cast<std::size_t> (source)];
                if (offered != currentRoles.role[static_cast<std::size_t> (to)])
                    offer = RoleOffer { to, offered };
            }
        }
        replaceSection (doc, encodeParameterValues (values));
        replaceSection (doc, SampleLibrary::encodeReferences (refs));
    }
    else if (info.type == PresetType::pattern)
    {
        const auto* seq = require (sequenceSectionTag);
        if (seq == nullptr)
            return juce::Result::fail ("the preset is incomplete");
        using Slots = std::array<StoredPattern, PatternStore::slotCount>;
        const auto in = std::make_unique<Slots>(), inB = std::make_unique<Slots>(), cur = std::make_unique<Slots>(),
                   curB = std::make_unique<Slots>();
        if (! PatternStore::decode (*seq, *in, *inB, error))
            return juce::Result::fail (error);
        PatternStore::factorySlots (*cur);
        *curB = *cur;
        if (const auto* s = doc.find (sequenceSectionTag))
            if (! PatternStore::decode (*s, *cur, *curB, error))
                return juce::Result::fail (error);
        const int slot = target >= 0 && target < PatternStore::slotCount ? target : processor_.currentSlot();
        (*cur)[static_cast<std::size_t> (slot)] = (*in)[0];
        (*curB)[static_cast<std::size_t> (slot)] = (*inB)[0];
        replaceSection (doc, PatternStore::encode (*cur, *curB));
    }
    else
    {
        const auto* seq = require (sequenceSectionTag);
        const auto* song = require (songSectionTag);
        if (seq == nullptr || song == nullptr)
            return juce::Result::fail ("the preset is incomplete");
        replaceSection (doc, *seq);
        replaceSection (doc, *song);
        if (const auto* g = require (generatorSectionTag))
            replaceSection (doc, *g);
    }

    const auto next = encodeBlob (doc);
    if (next.getSize() == 0)
        return juce::Result::fail ("the state is too large");
    processor_.setStateInformation (next.getData(), static_cast<int> (next.getSize()));
    const auto rejected = processor_.lastStateError();
    if (rejected.isNotEmpty())
        return juce::Result::fail (rejected);   // nothing changed
    previous_ = std::move (before);
    previousEpoch_ = processor_.patternEpoch();
    notice_ = notice;
    offer_ = offer;
    offerEpoch_ = previousEpoch_;
    return juce::Result::ok();
}

std::optional<Presets::RoleOffer> Presets::roleOffer() const
{
    if (! offer_ || processor_.patternEpoch() != offerEpoch_
        || processor_.padRoles().role[static_cast<std::size_t> (offer_->pad)] == offer_->role)
        return std::nullopt;
    return offer_;
}

bool Presets::applyRoleOffer()
{
    const auto offer = roleOffer();
    offer_.reset();
    return offer && processor_.setPadRole (offer->pad, offer->role);
}

juce::Result Presets::apply (const void* data, std::size_t size, int target, std::optional<PresetType> expected)
{
    preset::StateDocument doc;
    juce::String error;
    if (! decodeBlob (data, size, processor_, doc, error))
        return juce::Result::fail ("not a VOX Drums preset: " + error);
    return applyDocument (std::move (doc), target, expected);
}

juce::Result Presets::save (PresetType type, const juce::String& name, const juce::File& file, int index) const
{
    const auto block = capture (type, name, index);
    if (block.getSize() == 0)
        return juce::Result::fail ("the preset is too large");
    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("cannot create " + file.getParentDirectory().getFullPathName());
    // Written to a temporary file and moved: a failed write never leaves half a preset.
    juce::TemporaryFile temp (file);
    if (! temp.getFile().replaceWithData (block.getData(), block.getSize()) || ! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("cannot write " + file.getFullPathName());
    return juce::Result::ok();
}

juce::Result Presets::load (const juce::File& file, PresetType expected, int target)
{
    juce::MemoryBlock block;
    if (! file.existsAsFile() || file.getSize() > static_cast<juce::int64> (preset::maxStateBytes) || ! file.loadFileAsData (block))
        return juce::Result::fail ("cannot read " + file.getFullPathName());
    return apply (block.getData(), block.getSize(), target, expected);
}

juce::Result Presets::undoLoad()
{
    if (! canUndoLoad())
    {
        previous_.reset();   // of another project state: gone for good
        return juce::Result::fail ("nothing to undo");
    }
    processor_.setStateInformation (previous_.getData(), static_cast<int> (previous_.getSize()));
    const auto rejected = processor_.lastStateError();
    if (rejected.isNotEmpty())
        return juce::Result::fail (rejected);
    previous_.reset();
    return juce::Result::ok();
}

juce::Result Presets::loadFactoryKit (int kit)
{
    if (kit < 0 || kit >= factoryKitCount)
        return juce::Result::fail ("no such factory kit");
    preset::StateDocument doc;
    doc.product = processor_.product().stateTag;
    doc.schema = processor_.product().stateSchema;
    doc.sections.push_back (encodeInfo ({ PresetType::kit, factoryKitInfo (kit).name, 0 }));
    doc.sections.push_back (KitStore::encode (factoryKit (kit)));
    doc.sections.push_back (encodeParameterValues (factoryKitParameters (kit)));
    return applyDocument (std::move (doc), -1);
}

juce::Result Presets::loadFactoryPatterns (int kit)
{
    if (kit < 0 || kit >= factoryKitCount)
        return juce::Result::fail ("no such factory kit");
    const auto all = std::make_unique<PatternStore::Everything>();
    factoryPatterns (kit, *all);
    all->song.songMode = processor_.patterns().song().songMode;   // the playback mode is the user's
    preset::StateDocument doc;
    doc.product = processor_.product().stateTag;
    doc.schema = processor_.product().stateSchema;
    doc.sections.push_back (encodeInfo ({ PresetType::song, factoryKitInfo (kit).name, 0 }));
    doc.sections.push_back (PatternStore::encode (all->slots, all->alternates));
    doc.sections.push_back (encodeSong (all->song));
    return applyDocument (std::move (doc), -1);
}

//==============================================================================
// Portable kit: a ZIP with "kit.voxkitpreset" and "samples/<sha256><ext>".
juce::Result Presets::exportPortableKit (const juce::File& zip, const juce::String& name) const
{
    const auto block = capture (PresetType::kit, name);
    if (block.getSize() == 0)
        return juce::Result::fail ("the kit is too large");
    juce::ZipFile::Builder builder;
    builder.addEntry (std::make_unique<juce::MemoryInputStream> (block, true), 9, "kit.voxkitpreset", juce::Time());
    juce::StringArray added;
    for (int slot = 0; slot < slotCount; ++slot)
    {
        const auto info = processor_.samples().pad (slot);
        if (! info.reference)
            continue;
        const auto hex = hexOf (info.reference->sha256);
        if (added.contains (hex))
            continue;
        juce::File source (info.reference->path);
        if (! source.existsAsFile())
            for (const auto& ext : sampleExtensions)
                if (SampleLibrary::libraryFolder().getChildFile (hex + ext).existsAsFile())
                    source = SampleLibrary::libraryFolder().getChildFile (hex + ext);
        juce::MemoryBlock bytes;
        if (! source.existsAsFile() || ! source.loadFileAsData (bytes))
            return juce::Result::fail ("sample file is missing: " + info.reference->name);
        if (juce::SHA256 (bytes).toHexString() != hex)
            return juce::Result::fail ("sample file changed since it was assigned: " + info.reference->name);
        builder.addEntry (std::make_unique<juce::MemoryInputStream> (bytes, true), 0, "samples/" + hex + source.getFileExtension().toLowerCase(),
                          juce::Time());
        added.add (hex);
    }
    if (! zip.getParentDirectory().createDirectory())
        return juce::Result::fail ("cannot create " + zip.getParentDirectory().getFullPathName());
    juce::TemporaryFile temp (zip);
    {
        juce::FileOutputStream out (temp.getFile());
        if (! out.openedOk() || ! builder.writeToStream (out, nullptr))
            return juce::Result::fail ("cannot write " + zip.getFullPathName());
    }
    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("cannot write " + zip.getFullPathName());
    return juce::Result::ok();
}

juce::Result Presets::importPortableKit (const juce::File& zip)
{
    if (! zip.existsAsFile())
        return juce::Result::fail ("cannot read " + zip.getFullPathName());
    juce::ZipFile archive (zip);
    juce::MemoryBlock kitBlock;
    // Validate everything in memory before writing any file.
    struct Sample { juce::String hex, extension; juce::MemoryBlock bytes; };
    std::vector<Sample> samples;
    juce::int64 total = 0;
    for (int i = 0; i < archive.getNumEntries(); ++i)
    {
        const auto* entry = archive.getEntry (i);
        if (entry == nullptr)
            continue;
        if (entry->uncompressedSize < 0 || entry->uncompressedSize > SampleLimits::maxFileBytes
            || (total += entry->uncompressedSize) > SampleLimits::maxFileBytes)
            return juce::Result::fail ("the portable kit is too large");
        const std::unique_ptr<juce::InputStream> in (archive.createStreamForEntry (i));
        if (in == nullptr)
            return juce::Result::fail ("the portable kit is damaged");
        juce::MemoryBlock bytes;
        in->readIntoMemoryBlock (bytes, static_cast<juce::pointer_sized_int> (entry->uncompressedSize) + 1);
        if (static_cast<juce::int64> (bytes.getSize()) != entry->uncompressedSize)
            return juce::Result::fail ("the portable kit is damaged");
        if (entry->filename == "kit.voxkitpreset")
            kitBlock = std::move (bytes);
        else if (entry->filename.startsWith ("samples/"))
        {
            // The name must be exactly <64 hex digits><known extension>: never a path.
            const auto file = entry->filename.fromFirstOccurrenceOf ("samples/", false, false);
            const auto hex = file.upToLastOccurrenceOf (".", false, false);
            const auto extension = file.fromLastOccurrenceOf (".", true, false).toLowerCase();
            if (hex.length() != 64 || ! hex.containsOnly ("0123456789abcdef") || ! sampleExtensions.contains (extension))
                return juce::Result::fail ("the portable kit holds an unexpected file");
            if (juce::SHA256 (bytes).toHexString() != hex)
                return juce::Result::fail ("a sample in the portable kit does not match its hash");
            samples.push_back ({ hex, extension, std::move (bytes) });
        }
        else
            return juce::Result::fail ("the portable kit holds an unexpected file");
    }
    preset::StateDocument doc;
    juce::String error;
    PresetInfo info;
    if (kitBlock.getSize() == 0 || ! decodeBlob (kitBlock.getData(), kitBlock.getSize(), processor_, doc, error))
        return juce::Result::fail ("the portable kit holds no valid kit");
    const auto* infoSection = doc.find (presetTypeSectionTag);
    if (infoSection == nullptr || ! decodeInfo (*infoSection, info) || info.type != PresetType::kit)
        return juce::Result::fail ("the portable kit holds no valid kit");
    std::array<std::optional<SampleReference>, slotCount> refs;
    if (const auto* s = doc.find (sampleSectionTag))
        if (! SampleLibrary::decodeReferences (*s, refs, error))
            return juce::Result::fail (error);

    // The whole kit is checked before anything is written: its pad section
    // and parameters must decode (the rest is checked by the transactional
    // apply below, which rolls the files back if it refuses).
    {
        if (! complete (doc, PresetType::kit))
            return juce::Result::fail ("the portable kit holds no complete kit");
        auto kit = std::make_unique<kit::KitSettings> (defaultKit());
        if (! KitStore::decode (*doc.find (padSectionTag), *kit, error))
            return juce::Result::fail ("the portable kit holds no valid kit: " + error);
    }
    // Only the samples the kit references go into the user library (by
    // hash; existing files are kept), then the references point at them.
    const auto folder = SampleLibrary::libraryFolder();
    std::vector<juce::File> written;
    const auto rollBack = [&written] {
        for (const auto& file : written)
            file.deleteFile();
    };
    for (const auto& sample : samples)
    {
        bool referenced = false;
        for (const auto& ref : refs)
            referenced = referenced || (ref && hexOf (ref->sha256) == sample.hex);
        if (! referenced)
            continue;
        const auto target = folder.getChildFile (sample.hex + sample.extension);
        if (! target.existsAsFile())
        {
            juce::TemporaryFile temp (target);
            if (! folder.createDirectory() || ! temp.getFile().replaceWithData (sample.bytes.getData(), sample.bytes.getSize())
                || ! temp.overwriteTargetFileWithTemporary())
            {
                rollBack();
                return juce::Result::fail ("cannot write " + target.getFullPathName());
            }
            written.push_back (target);
        }
        for (auto& ref : refs)
            if (ref && hexOf (ref->sha256) == sample.hex)
                ref->path = target.getFullPathName();
    }
    replaceSection (doc, SampleLibrary::encodeReferences (refs));
    const auto result = applyDocument (std::move (doc), -1, PresetType::kit);
    if (result.failed())
        rollBack();   // a refused kit leaves the library as it was
    return result;
}

} // namespace vox::drums
