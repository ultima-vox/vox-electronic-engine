#include "StyleLibrary.h"

#include "Generator.h"
#include "StyleRegistry.h"

#include <algorithm>

namespace vox::drums {

juce::File StyleLibrary::defaultFolder()
{
    const auto overridden = juce::SystemStats::getEnvironmentVariable ("VOX_DRUMS_STYLES", {});
    if (overridden.isNotEmpty() && juce::File::isAbsolutePath (overridden))
        return juce::File (overridden);
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("ULTIMA VOX")
        .getChildFile ("VOX Drums")
        .getChildFile ("Styles");
}

juce::File StyleLibrary::fileFor (const style::Definition& d) const
{
    return folder_.getChildFile (d.key + "-" + d.id.substring (0, 8) + ".voxstyle");
}

std::vector<StyleLibrary::Entry> StyleLibrary::list (juce::StringArray* skipped) const
{
    std::vector<Entry> out;
    auto files = folder_.findChildFiles (juce::File::findFiles, false, "*.voxstyle");
    files.sort();
    for (const auto& file : files)
    {
        auto d = std::make_shared<style::Definition>();
        const auto parsed = style::parse (file.loadFileAsString(), *d);
        if (parsed.failed())
        {
            if (skipped != nullptr)
                skipped->add (file.getFileName() + ": " + parsed.getErrorMessage());
            continue;
        }
        const auto hash = style::contentHash (*d);
        out.push_back ({ file, std::move (d), hash });
    }
    std::stable_sort (out.begin(), out.end(), [] (const Entry& a, const Entry& b) {
        const auto na = a.definition->name.toLowerCase(), nb = b.definition->name.toLowerCase();
        if (na != nb)
            return na < nb;
        if (a.definition->meta.author != b.definition->meta.author)
            return a.definition->meta.author < b.definition->meta.author;
        return a.definition->id < b.definition->id;
    });
    return out;
}

std::shared_ptr<const style::Definition> StyleLibrary::find (const juce::String& id) const
{
    for (const auto& e : list())
        if (e.definition->id == id)
            return e.definition;
    return nullptr;
}

StyleLibrary::ImportResult StyleLibrary::importText (const juce::String& text, Collision onConflict)
{
    using Status = ImportResult::Status;
    ImportResult r;
    auto d = std::make_shared<style::Definition>();
    if (const auto parsed = style::parse (text, *d); parsed.failed())
    {
        r.message = "not a valid style: " + parsed.getErrorMessage();
        return r;
    }
    if (style::infoByUuid (d->id) != nullptr)
    {
        r.message = "factory styles cannot be replaced";
        return r;
    }
    // Self-consistency only: the file's own assertions on a generated matrix.
    if (const auto violations = checkStyleAssertions (d, 1); ! violations.isEmpty())
    {
        r.message = "the style breaks its own assertions: " + violations[0];
        return r;
    }
    if (const auto made = folder_.createDirectory(); made.failed())
    {
        r.message = "cannot create the library folder: " + made.getErrorMessage();
        return r;
    }
    const auto hash = style::contentHash (*d);
    const Entry* same = nullptr;
    const auto entries = list();
    for (const auto& e : entries)
        if (e.definition->id == d->id)
            same = &e;
    const auto write = [&] (const style::Definition& def, const juce::File& target) {
        return target.replaceWithText (style::serialize (def));
    };
    if (same == nullptr)
    {
        if (! write (*d, fileFor (*d)))
        {
            r.message = "cannot write " + fileFor (*d).getFullPathName();
            return r;
        }
        r.status = Status::added;
        r.message = "Added " + d->name;
        r.definition = d;
        return r;
    }
    if (same->contentHash == hash)
    {
        r.status = Status::alreadyInLibrary;
        r.message = d->name + " is already in the library";
        r.definition = same->definition;
        return r;
    }
    if (onConflict == Collision::ask)
    {
        r.status = Status::conflict;
        r.message = "The library has another version of " + same->definition->name + ": Replace it, or Keep both";
        r.definition = same->definition;
        return r;
    }
    if (onConflict == Collision::replace)
    {
        const auto old = same->file;
        if (! write (*d, fileFor (*d)))
        {
            r.message = "cannot write " + fileFor (*d).getFullPathName();
            return r;
        }
        if (old != fileFor (*d))
            old.deleteFile();
        r.status = Status::replaced;
        r.message = "Replaced " + d->name + " (projects keep their own copy)";
        r.definition = d;
        return r;
    }
    // Keep both: the imported copy gets a new id and " (n)" in its name.
    auto copy = std::make_shared<style::Definition> (*d);
    copy->id = juce::Uuid().toDashedString().toLowerCase();
    juce::StringArray names;
    for (const auto& e : entries)
        names.add (e.definition->name);
    for (int n = 2;; ++n)
    {
        const auto suffix = " (" + juce::String (n) + ")";
        const auto candidate = d->name.substring (0, style::maxMetaText - suffix.length()) + suffix;
        if (! names.contains (candidate))
        {
            copy->name = candidate;
            break;
        }
    }
    // Re-parse the canonical text so the stored copy is exactly what a reader gets.
    auto stored = std::make_shared<style::Definition>();
    if (style::parse (style::serialize (*copy), *stored).failed() || ! write (*stored, fileFor (*stored)))
    {
        r.message = "cannot write the copy of " + d->name;
        return r;
    }
    r.status = Status::keptBoth;
    r.message = "Kept both: the copy is " + stored->name;
    r.definition = stored;
    return r;
}

StyleLibrary::ImportResult StyleLibrary::importFile (const juce::File& file, Collision onConflict)
{
    if (! file.existsAsFile())
    {
        ImportResult r;
        r.message = "no such file: " + file.getFullPathName();
        return r;
    }
    if (file.getSize() > static_cast<juce::int64> (style::maxTextBytes))
    {
        ImportResult r;
        r.message = "the file is larger than " + juce::String (style::maxTextBytes) + " bytes";
        return r;
    }
    return importText (file.loadFileAsString(), onConflict);
}

juce::Result StyleLibrary::exportStyle (const style::Definition& definition, const juce::File& file)
{
    return file.replaceWithText (style::serialize (definition)) ? juce::Result::ok() : juce::Result::fail ("cannot write " + file.getFullPathName());
}

bool StyleLibrary::remove (const juce::String& id)
{
    for (const auto& e : list())
        if (e.definition->id == id)
            return e.file.deleteFile();
    return false;
}

juce::String StyleLibrary::projectNotice (const GeneratorSettings& generator) const
{
    if (generator.custom == nullptr)
        return {};
    for (const auto& e : list())
        if (e.definition->id == generator.custom->id && e.contentHash != style::contentHash (*generator.custom))
            return "This project uses its own version of " + generator.custom->name;
    return {};
}

} // namespace vox::drums
