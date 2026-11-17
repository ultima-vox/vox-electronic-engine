// Filesystem regressions from review #151. Uses the real parser, generator
// assertions and filesystem; the Windows sharing violation runs on Windows CI.
#include "Generator.h"
#include "StyleLibrary.h"

#include <cstdio>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

using namespace vox::drums;
using Status = StyleLibrary::ImportResult::Status;
using Collision = StyleLibrary::Collision;

namespace {
int failures = 0;
void check (bool value, const char* message)
{
    if (! value)
    {
        std::printf ("FAIL [style library]: %s\n", message);
        ++failures;
    }
}
}

int main (int argc, char** argv)
{
    if (argc != 2)
        return 2;
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("vox-style-library-regression-" + juce::Uuid().toString());
    // Exercise native Windows UTF-16 paths, including a surrogate pair:
    // Cyrillic, CJK and a non-BMP musical symbol. Keep the source ASCII so
    // this regression does not depend on the compiler's source code page.
    const auto unicodeName = juce::String::charToString (0x0416)
                           + juce::String::charToString (0x97f3)
                           + juce::String::charToString (0x1f3b5);
    StyleLibrary library (root.getChildFile ("styles-" + unicodeName));
    const auto original = juce::File (juce::String (argv[1])).loadFileAsString();
    style::Definition definition;
    if (style::parse (original, definition).failed())
        return 2;
    const auto canonical = style::serialize (definition);
    auto renamed = definition;
    renamed.key = "renamed-user-style";
    renamed.rules[0].velocity = 0.9f;
    const auto renamedText = style::serialize (renamed);
    const auto destination = library.folder().getChildFile (renamed.key + "-" + renamed.id.substring (0, 8) + ".voxstyle");
    const auto files = [&] { return library.folder().findChildFiles (juce::File::findFiles, false, "*.voxstyle"); };

    // Direct UTF-8 byte bound, before parsing or creating the library folder.
    const auto oversized = canonical + juce::String::repeatedString (" ", static_cast<int> (style::maxTextBytes));
    const auto unicode = canonical + juce::String::repeatedString (juce::String::charToString (0x0436), static_cast<int> (style::maxTextBytes / 2));
    check (unicode.length() < static_cast<int> (style::maxTextBytes) && unicode.getNumBytesAsUTF8() > style::maxTextBytes,
           "multibyte input distinguishes character and byte bounds");
    for (const auto& text : { oversized, unicode })
    {
        const auto result = library.importText (text);
        check (result.status == Status::refused && result.message.contains ("UTF-8") && ! library.folder().exists(),
               "oversized direct input does not create a folder");
    }

    check (library.importText (original).status == Status::added, "fixture imports into a non-ASCII folder");
    const auto imported = library.list();
    if (imported.empty())
    {
        root.deleteRecursively();
        return 1;
    }
    const auto old = library.folder().getChildFile ("original-" + unicodeName + ".voxstyle");
    check (imported.front().file.moveFileTo (old), "fixture gets a non-ASCII filename");
    const auto unicodeEntries = library.list();
    check (unicodeEntries.size() == 1 && unicodeEntries.front().file == old && old.loadFileAsString() == canonical,
           "non-ASCII folder and filename round-trip without content loss");

    // A different UUID with the same truncated prefix/key must not be lost.
    auto collision = renamed;
    collision.id = renamed.id.substring (0, 8) + "-1111-4222-8333-444455556666";
    const auto collisionText = style::serialize (collision);
    check (destination.replaceWithText (collisionText), "destination collision fixture writes");
    auto result = library.importText (renamedText, Collision::replace);
    check (result.status == Status::refused && old.loadFileAsString() == canonical && destination.loadFileAsString() == collisionText,
           "Replace never overwrites another UUID at the key-derived destination");
    check (destination.deleteFile(), "remove collision fixture");

    // The reviewed failure: old file cannot be deleted/renamed on Windows.
   #if JUCE_WINDOWS
    const auto handle = CreateFileW (old.getFullPathName().toWideCharPointer(), GENERIC_READ,
                                    FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check (handle != INVALID_HANDLE_VALUE, "lock original against deletion");
    if (handle != INVALID_HANDLE_VALUE)
    {
        check (! old.deleteFile(), "Windows sharing mode really prevents old-file deletion");
        result = library.importText (renamedText, Collision::replace);
        check (result.status == Status::refused && old.loadFileAsString() == canonical
                   && ! destination.exists() && files().size() == 1 && library.list().size() == 1,
               "failed key rename preserves original bytes and exactly one UUID entry");
        result = library.importText (canonical, Collision::replace);
        check (result.status == Status::alreadyInLibrary, "locked identical input is a no-op");
        auto sameKey = definition;
        sameKey.rules[0].velocity = 0.9f;
        result = library.importText (style::serialize (sameKey), Collision::replace);
        check (result.status == Status::refused && old.loadFileAsString() == canonical && files().size() == 1,
               "failed same-path replacement preserves original bytes");
        CloseHandle (handle);
    }
    check (old.setReadOnly (true), "mark original read-only");
    result = library.importText (renamedText, Collision::replace);
    check (result.status == Status::refused && old.loadFileAsString() == canonical
               && ! destination.exists() && files().size() == 1,
           "read-only replacement failure restores the original name and content");
    check (old.setReadOnly (false), "restore original write access");
   #endif

    result = library.importText (renamedText, Collision::replace);
    check (result.status == Status::replaced && ! old.exists() && destination.loadFileAsString() == renamedText
               && files().size() == 1 && library.list().size() == 1,
           "successful key rename leaves one canonical file and the updated definition");
    check (library.find (definition.id) != nullptr && library.find (definition.id)->key == renamed.key,
           "find observes replacement");

    // Same UUID, different keys: neither copy is authoritative, even if one
    // happens to match the imported content. All collision choices refuse.
    check (old.replaceWithText (canonical), "duplicate UUID fixture writes");
    for (const auto choice : { Collision::ask, Collision::replace, Collision::keepBoth })
    {
        result = library.importText (canonical, choice);
        check (result.status == Status::refused && result.message.contains ("duplicate style UUID"),
               "ambiguous UUID refuses every import choice");
    }
    juce::StringArray skipped, repeated;
    check (library.list (&skipped).empty() && library.list (&repeated).empty()
               && skipped.size() == 2 && skipped == repeated && skipped[0].contains (definition.id) && skipped[1].contains (definition.id),
           "list excludes both UUID duplicates with deterministic diagnostics");
    check (library.find (definition.id) == nullptr && ! library.remove (definition.id)
               && old.loadFileAsString() == canonical && destination.loadFileAsString() == renamedText && files().size() == 2,
           "find and remove refuse ambiguity without mutating either file");
    for (const auto& text : { oversized, unicode })
        check (library.importText (text).status == Status::refused && old.loadFileAsString() == canonical
                   && destination.loadFileAsString() == renamedText && files().size() == 2,
               "oversized direct input leaves an existing library unchanged");

    check (old.deleteFile(), "remove duplicate fixture");
    check (library.find (definition.id) != nullptr && library.list().size() == 1, "library recovers after explicit duplicate repair");
    const auto hugeFile = library.folder().getChildFile ("oversized.voxstyle");
    check (hugeFile.replaceWithText (oversized), "oversized on-disk fixture writes");
    skipped.clear();
    check (library.list (&skipped).size() == 1 && skipped.size() == 1 && skipped[0].startsWith ("oversized.voxstyle"),
           "oversized on-disk file is bounded before loading and reported");
    check (library.importFile (hugeFile).status == Status::refused, "oversized file import refuses");
    check (library.remove (definition.id) && library.find (definition.id) == nullptr, "unique entry removes normally");
    check (library.folder().findChildFiles (juce::File::findFiles, false, "*.tmp").isEmpty(),
           "completed operations leave no staging files");
    check (root.deleteRecursively(), "temporary fixtures cleaned up");
    if (failures == 0)
        std::printf ("Style library filesystem regressions passed\n");
    return failures == 0 ? 0 : 1;
}
