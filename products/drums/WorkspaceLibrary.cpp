#include "WorkspaceLibrary.h"

namespace vox::drums {
namespace {
bool validName (const juce::String& name)
{
    return name.length() >= 1 && name.length() <= WorkspaceLibrary::maxNameCharacters && name.trim().isNotEmpty();
}
bool uniqueName (const std::vector<WorkspaceLibrary::Entry>& entries, const juce::String& name)
{
    return std::none_of (entries.begin(), entries.end(), [&] (const auto& entry) { return entry.name.equalsIgnoreCase (name); });
}
bool properties (const juce::var& value, const char* a, const char* b)
{
    const auto* object = value.getDynamicObject();
    return object != nullptr && object->getProperties().size() == 2 && object->hasProperty (a) && object->hasProperty (b);
}
bool parse (const juce::String& text, std::vector<WorkspaceLibrary::Entry>& entries)
{
    juce::var root;
    if (juce::JSON::parse (text, root).failed() || ! properties (root, "version", "workspaces")
        || ! root["version"].isInt() || static_cast<int> (root["version"]) != 1) return false;
    const auto* list = root["workspaces"].getArray();
    if (list == nullptr || list->size() > WorkspaceLibrary::maxEntries) return false;
    for (const auto& item : *list)
    {
        if (! properties (item, "name", "layout") || ! item["name"].isString() || ! item["layout"].isString()) return false;
        const auto name = item["name"].toString(), base64 = item["layout"].toString();
        if (! validName (name) || ! uniqueName (entries, name) || base64.length() > 4 * ((static_cast<int> (layout::maxBytes) + 2) / 3)) return false;
        juce::MemoryOutputStream decoded;
        if (! juce::Base64::convertFromBase64 (decoded, base64) || decoded.getDataSize() > layout::maxBytes) return false;
        auto result = layout::decode ({ static_cast<const layout::Byte*> (decoded.getData()), decoded.getDataSize() });
        if (result.status != layout::Status::valid) return false;
        entries.push_back ({ name, std::move (result.document) });
    }
    return true;
}
} // namespace

juce::File WorkspaceLibrary::userFile()
{
    const auto folder = juce::SystemStats::getEnvironmentVariable ("VOX_DRUMS_WORKSPACES", {});
    const auto root = folder.isNotEmpty() ? juce::File (folder)
        : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("ULTIMA VOX/VOX Drums/Workspaces");
    return root.getChildFile ("workspaces.json");
}

WorkspaceLibrary::Read WorkspaceLibrary::read() const
{
    Read result;
    if (! file_.exists()) return result;
    if (! file_.existsAsFile()) return { {}, "Cannot read workspaces: the path is not a file" };
    bool corrupt = file_.getSize() > maxFileBytes;
    if (! corrupt)
    {
        juce::FileInputStream stream (file_);
        if (stream.failedToOpen()) return { {}, "Cannot read workspaces" };
        juce::MemoryBlock bytes;
        stream.readIntoMemoryBlock (bytes, maxFileBytes + 1);
        corrupt = bytes.getSize() > static_cast<std::size_t> (maxFileBytes)
            || ! parse (juce::String::fromUTF8 (static_cast<const char*> (bytes.getData()), static_cast<int> (bytes.getSize())), result.entries);
    }
    if (corrupt)
    {
        result.entries.clear();
        const auto bad = file_.getSiblingFile ("workspaces.json.bad").getNonexistentSibling();
        result.diagnostic = file_.moveFileTo (bad) ? "Invalid workspaces file moved to " + bad.getFileName()
                                                : "Invalid workspaces file; cannot move it aside";
    }
    return result;
}

juce::Result WorkspaceLibrary::write (const std::vector<Entry>& entries) const
{
    if (entries.size() > maxEntries) return juce::Result::fail ("At most 32 user workspaces are allowed");
    juce::Array<juce::var> list;
    std::vector<Entry> seen;
    for (const auto& entry : entries)
    {
        const auto bytes = layout::encode (entry.layout);
        if (! validName (entry.name) || ! uniqueName (seen, entry.name) || ! bytes)
            return juce::Result::fail ("Workspace names must be unique (1-64 characters) and layouts valid");
        seen.push_back (entry);
        auto object = std::make_unique<juce::DynamicObject>();
        object->setProperty ("name", entry.name);
        object->setProperty ("layout", juce::Base64::toBase64 (bytes->data(), bytes->size()));
        list.add (juce::var (object.release()));
    }
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("version", 1);
    root->setProperty ("workspaces", list);
    const auto text = juce::JSON::toString (juce::var (root.release()), true);
    if (text.getNumBytesAsUTF8() > static_cast<std::size_t> (maxFileBytes)) return juce::Result::fail ("Workspaces file is too large");
    if (file_.exists() && ! file_.existsAsFile()) return juce::Result::fail ("Workspace path is not a file");
    if (file_.getParentDirectory().createDirectory().failed()) return juce::Result::fail ("Cannot create workspace folder");
    juce::TemporaryFile temp (file_);
    {
        juce::FileOutputStream stream (temp.getFile());
        if (stream.failedToOpen() || ! stream.writeText (text, false, false, nullptr)) return juce::Result::fail ("Cannot write workspaces");
        stream.flush();
        if (stream.getStatus().failed()) return stream.getStatus();
    }
    if (! temp.overwriteTargetFileWithTemporary()) return juce::Result::fail ("Cannot replace workspaces file");
    return juce::Result::ok();
}
} // namespace vox::drums
