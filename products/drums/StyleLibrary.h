#pragma once

// VOX Drums user style library (PR 137 §11.2; message thread only). The
// folder holds user styles as canonical .voxstyle text, one file per style
// (named <key>-<first 8 digits of the id>.voxstyle). Projects never depend
// on it: a project carries its user style as DSTL and restores from that,
// whatever the library holds.
//
// Import is strict: the file must parse (every rejection names its field),
// must hold its own assertions on a generated matrix (checkStyleAssertions;
// this proves only that the file agrees with itself, not genre or quality),
// and is then compared with the library by id:
//   same id, same contentHash   -> already in the library, nothing changes
//   same id, other contentHash  -> a conflict: the caller chooses Replace
//                                  (the library entry changes; projects keep
//                                  their embedded copy) or Keep both (the
//                                  copy gets a new id and " (2)" in its name)
//   a factory style's id        -> refused: factory styles cannot be replaced
//   another id with the same name -> allowed (the list shows the author)

#include "StyleDefinition.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

namespace vox::drums {

struct GeneratorSettings;

class StyleLibrary {
public:
    // VOX_DRUMS_STYLES (an absolute path) overrides the folder (tests,
    // portable setups); otherwise <user application data>/ULTIMA VOX/VOX Drums/Styles.
    static juce::File defaultFolder();
    explicit StyleLibrary (juce::File folder = defaultFolder()) : folder_ (std::move (folder)) {}

    struct Entry {
        juce::File file;
        std::shared_ptr<const style::Definition> definition;
        juce::String contentHash;
    };
    // The valid styles of the folder, by name then author then id. A file
    // that does not parse, exceeds the input bound, or shares its UUID with
    // another file is listed in `skipped` (file name: reason), never offered.
    std::vector<Entry> list (juce::StringArray* skipped = nullptr) const;
    std::shared_ptr<const style::Definition> find (const juce::String& id) const;

    enum class Collision { ask, replace, keepBoth };
    struct ImportResult {
        enum class Status { added, alreadyInLibrary, replaced, keptBoth, conflict, refused } status = Status::refused;
        juce::String message;                               // why it was refused, or what happened
        std::shared_ptr<const style::Definition> definition;   // the style as it is now in the library
    };
    // `conflict`: same id with other content and `onConflict` == ask; nothing
    // was written; call again with replace or keepBoth.
    ImportResult importText (const juce::String& text, Collision onConflict = Collision::ask);
    ImportResult importFile (const juce::File& file, Collision onConflict = Collision::ask);

    // Writes `definition` as canonical .voxstyle text.
    static juce::Result exportStyle (const style::Definition& definition, const juce::File& file);
    // Removes the library entry with this id; false if there is none.
    bool remove (const juce::String& id);

    // PR 137 §11.2: when the generator runs an embedded user style whose id
    // the library holds with other content: "This project uses its own
    // version of <name>". Empty otherwise.
    juce::String projectNotice (const GeneratorSettings& generator) const;

    const juce::File& folder() const noexcept { return folder_; }

private:
    std::vector<Entry> scan (juce::StringArray* skipped = nullptr) const;
    juce::File fileFor (const style::Definition&) const;
    juce::File folder_;
};

} // namespace vox::drums
