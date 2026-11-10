// Frozen VST3 identities (cmake/suite/SuiteIdentity.cmake, IDENTITY.md):
// the component class ID that JUCE derives from the manufacturer and plugin
// codes must equal the frozen CID, all products must differ, and none may
// reuse the legacy Rack plugin code.
#include <juce_core/juce_core.h>
#include <juce_audio_processors_headless/utilities/juce_VST3Interface.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
std::uint32_t fourCC (const char* code)
{
    return (static_cast<std::uint32_t> (static_cast<unsigned char> (code[0])) << 24)
         | (static_cast<std::uint32_t> (static_cast<unsigned char> (code[1])) << 16)
         | (static_cast<std::uint32_t> (static_cast<unsigned char> (code[2])) << 8)
         |  static_cast<std::uint32_t> (static_cast<unsigned char> (code[3]));
}

// The CID string of a VST3 FUID (as written to moduleinfo.json): the four
// 32-bit words in order. JUCE stores the bytes in COM order on Windows.
std::string canonicalCid (const juce::VST3Interface::Id& id)
{
    std::array<int, 16> order { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
   #if JUCE_WINDOWS
    order = { 3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15 };
   #endif
    std::string hex;
    for (int index : order)
    {
        char buffer[3];
        std::snprintf (buffer, sizeof (buffer), "%02X", std::to_integer<unsigned> (id[static_cast<std::size_t> (index)]));
        hex += buffer;
    }
    return hex;
}
} // namespace

int main()
{
    struct Product { const char* name; const char* code; const char* cid; };
    const Product products[] = {
        { "VOX Trance Synthesis", VOX_ID_SYN_CODE, VOX_ID_SYN_CID },
        { "VOX Drums", VOX_ID_DRM_CODE, VOX_ID_DRM_CID },
        { "VOX FX", VOX_ID_FX_CODE, VOX_ID_FX_CID },
    };
    int failures = 0;
    const auto manufacturer = fourCC (VOX_ID_MANUFACTURER);
    for (std::size_t i = 0; i < std::size (products); ++i)
    {
        const auto& p = products[i];
        const auto computed = canonicalCid (juce::VST3Interface::jucePluginId (manufacturer, fourCC (p.code)));
        std::printf ("%-22s %s %s\n", p.name, p.code, computed.c_str());
        if (computed != p.cid)
        {
            std::printf ("FAIL: %s CID %s != frozen %s\n", p.name, computed.c_str(), p.cid);
            ++failures;
        }
        if (std::string (p.code) == "DkP1")
        {
            std::printf ("FAIL: %s reuses the legacy Rack plugin code\n", p.name);
            ++failures;
        }
        for (std::size_t j = 0; j < i; ++j)
            if (std::string (p.code) == products[j].code || std::string (p.cid) == products[j].cid)
            {
                std::printf ("FAIL: %s and %s share an identity\n", p.name, products[j].name);
                ++failures;
            }
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
