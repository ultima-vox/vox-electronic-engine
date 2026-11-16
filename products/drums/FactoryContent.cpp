#include "FactoryContent.h"

#include "DrumsParameters.h"
#include "KitStore.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace vox::drums {
namespace {

using kit::Filter;
using kit::Model;

struct PadRow {
    Model model;
    std::uint8_t note, choke;
    float level, tune, decay, pan, character, cutoff;
    Filter filter;
    float resonance, attack, sustain, pitchAmount, pitchDecay, drive, sub, tail, send1, send2;
};

struct KitTable {
    FactoryKitInfo info;
    float volume;
    int delayTime;
    float delayFeedback, delayTone, reverbSize, reverbDamp;
    PadRow pads[kit::padCount];
};

// Kit Volume (the kit's output gain on every bus) is calibrated per kit
// (TL P1 review on #148): with the worst-case factory content measured by
// suite_drums_gain_staging_tests (a multi-seed stress matrix) it peaks at
// about -2 dBFS sample peak or below (the gate is -1 dBFS) on Main and on
// every output bus. Before calibration every kit had
// 0.6 (tests/suite/golden/factory-content-before-gain-staging.csv).
// Pad layout shared by every kit (the generator's lane roles): kick, snare,
// clap, closed hat, open hat, tom, rim, cymbal, then eight percussion pads.
const KitTable tables[originalFactoryKitCount] = {
    // Psy / Full-on: short punchy kick with a fast pitch drop, tight bright hats, dry.
    { { "Psy Full-on", "Psy / Full-on", { Style::fullOn, Style::goa }, 145.0f }, 0.385f, 3, 0.3f, 0.6f, 0.35f, 0.6f,
      {
          { Model::kick, 36, 0, 0.9f, 0.0f, 0.3f, 0.0f, 0.55f, 20000.0f, Filter::off, 0.0f, 0.2f, -0.1f, 36.0f, 0.022f, 2.2f, 0.25f, 0.2f, 0.0f, 0.0f },
          { Model::snare, 38, 0, 0.6f, 2.0f, 0.16f, 0.0f, 0.7f, 20000.0f, Filter::off, 0.0f, 0.1f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.15f },
          { Model::clap, 39, 0, 0.55f, 0.0f, 0.22f, -0.1f, 0.6f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.1f, 0.2f },
          { Model::hat, 42, 1, 0.45f, 3.0f, 0.045f, 0.2f, 0.7f, 20000.0f, Filter::hp12, 0.1f, 0.2f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.0f },
          { Model::hat, 46, 1, 0.45f, 3.0f, 0.28f, 0.2f, 0.7f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.1f },
          { Model::tom, 45, 0, 0.55f, -2.0f, 0.3f, -0.3f, 0.45f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 30.0f, 0.05f, 1.8f, 0.3f, 0.25f, 0.15f, 0.1f },
          { Model::rim, 37, 0, 0.5f, 4.0f, 0.06f, 0.3f, 0.6f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.25f, 0.0f },
          { Model::cymbal, 49, 0, 0.35f, 0.0f, 1.4f, -0.3f, 0.6f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.3f },
          { Model::perc, 50, 0, 0.5f, 7.0f, 0.09f, -0.4f, 0.7f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.3f, 0.0f },
          { Model::perc, 51, 0, 0.5f, 12.0f, 0.07f, 0.4f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.3f, 0.0f },
          { Model::tom, 52, 0, 0.5f, 5.0f, 0.2f, 0.25f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 30.0f, 0.04f, 1.6f, 0.3f, 0.25f, 0.1f, 0.1f },
          { Model::tom, 53, 0, 0.5f, 10.0f, 0.16f, -0.25f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 30.0f, 0.04f, 1.6f, 0.3f, 0.25f, 0.1f, 0.1f },
          { Model::perc, 54, 0, 0.45f, -5.0f, 0.14f, 0.5f, 0.3f, 20000.0f, Filter::bp, 0.3f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.2f, 0.1f },
          { Model::rim, 55, 0, 0.45f, -3.0f, 0.1f, -0.5f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.2f, 0.0f },
          { Model::hat, 56, 0, 0.4f, 8.0f, 0.03f, 0.6f, 0.8f, 20000.0f, Filter::hp24, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.0f },
          { Model::cymbal, 57, 0, 0.35f, 5.0f, 0.6f, 0.0f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.35f },
      } },
    // House / Techno: round longer kick, noisy clap with room, shuffling hats, rumble toms.
    { { "House Techno", "House / Techno", { Style::house, Style::techno }, 126.0f }, 0.407f, 2, 0.35f, 0.45f, 0.5f, 0.5f,
      {
          { Model::kick, 36, 0, 0.9f, -2.0f, 0.45f, 0.0f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.2f, 20.0f, 0.05f, 1.4f, 0.5f, 0.4f, 0.0f, 0.0f },
          { Model::snare, 38, 0, 0.55f, -1.0f, 0.2f, 0.0f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.25f },
          { Model::clap, 39, 0, 0.65f, -1.0f, 0.3f, 0.0f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.1f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.35f },
          { Model::hat, 42, 1, 0.45f, 0.0f, 0.06f, 0.15f, 0.5f, 20000.0f, Filter::hp12, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.05f },
          { Model::hat, 46, 1, 0.5f, -1.0f, 0.4f, 0.15f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.15f },
          { Model::tom, 45, 0, 0.5f, -9.0f, 0.5f, 0.0f, 0.3f, 2500.0f, Filter::lp24, 0.2f, 0.0f, 0.2f, 12.0f, 0.08f, 2.5f, 0.6f, 0.5f, 0.0f, 0.2f },
          { Model::rim, 37, 0, 0.5f, 0.0f, 0.07f, 0.35f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.3f, 0.1f },
          { Model::cymbal, 49, 0, 0.35f, -3.0f, 2.2f, -0.2f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.3f },
          { Model::perc, 50, 0, 0.5f, 2.0f, 0.12f, -0.35f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.25f, 0.1f },
          { Model::perc, 51, 0, 0.5f, 6.0f, 0.1f, 0.35f, 0.6f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.25f, 0.1f },
          { Model::clap, 52, 0, 0.45f, 5.0f, 0.12f, 0.3f, 0.7f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.3f },
          { Model::tom, 53, 0, 0.5f, 3.0f, 0.25f, -0.3f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.04f, 1.6f, 0.3f, 0.25f, 0.1f, 0.15f },
          { Model::perc, 54, 0, 0.45f, -7.0f, 0.2f, 0.5f, 0.2f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.2f, 0.2f },
          { Model::rim, 55, 0, 0.45f, 7.0f, 0.05f, -0.5f, 0.7f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.35f, 0.0f },
          { Model::hat, 56, 0, 0.4f, 5.0f, 0.1f, 0.55f, 0.3f, 20000.0f, Filter::bp, 0.2f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.1f, 0.1f },
          { Model::cymbal, 57, 0, 0.35f, 2.0f, 0.9f, 0.0f, 0.7f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.4f },
      } },
    // Hip-hop / Breaks: long sub kick, fat low snare, crisp rolling hats, wide room.
    { { "Hip-hop Breaks", "Hip-hop / Breaks", { Style::trap, Style::breaks }, 92.0f }, 0.327f, 4, 0.25f, 0.4f, 0.6f, 0.45f,
      {
          { Model::kick, 36, 0, 0.9f, -5.0f, 0.9f, 0.0f, 0.3f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.4f, 16.0f, 0.07f, 3.0f, 0.8f, 0.9f, 0.0f, 0.0f },
          { Model::snare, 38, 0, 0.7f, -4.0f, 0.3f, 0.0f, 0.45f, 20000.0f, Filter::off, 0.0f, 0.3f, 0.1f, 24.0f, 0.035f, 2.0f, 0.3f, 0.25f, 0.0f, 0.2f },
          { Model::clap, 39, 0, 0.55f, -3.0f, 0.4f, 0.1f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.3f },
          { Model::hat, 42, 1, 0.4f, 6.0f, 0.035f, 0.1f, 0.8f, 20000.0f, Filter::hp24, 0.0f, 0.1f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.0f },
          { Model::hat, 46, 1, 0.45f, 4.0f, 0.6f, 0.1f, 0.6f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.15f },
          { Model::tom, 45, 0, 0.6f, -7.0f, 0.6f, -0.3f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.06f, 1.8f, 0.4f, 0.4f, 0.0f, 0.2f },
          { Model::rim, 37, 0, 0.5f, -2.0f, 0.09f, 0.3f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.15f, 0.15f },
          { Model::cymbal, 49, 0, 0.35f, -5.0f, 2.8f, -0.3f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.3f },
          { Model::perc, 50, 0, 0.5f, -4.0f, 0.18f, -0.4f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.2f, 0.15f },
          { Model::perc, 51, 0, 0.5f, 3.0f, 0.12f, 0.4f, 0.6f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.2f, 0.15f },
          { Model::snare, 52, 0, 0.45f, 7.0f, 0.1f, 0.2f, 0.8f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.1f, 0.1f },
          { Model::kick, 53, 0, 0.6f, 7.0f, 0.2f, 0.0f, 0.7f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 30.0f, 0.02f, 4.0f, 0.1f, 0.1f, 0.0f, 0.0f },
          { Model::perc, 54, 0, 0.45f, -10.0f, 0.3f, 0.5f, 0.2f, 20000.0f, Filter::lp12, 0.3f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.1f, 0.25f },
          { Model::rim, 55, 0, 0.45f, 5.0f, 0.06f, -0.5f, 0.6f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.3f, 0.0f },
          { Model::hat, 56, 0, 0.4f, 10.0f, 0.02f, 0.5f, 0.9f, 20000.0f, Filter::hp24, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.0f },
          { Model::cymbal, 57, 0, 0.3f, 7.0f, 0.5f, 0.0f, 0.8f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.4f },
      } },
};


//==============================================================================
// PR 137 §5: the genre kits. Synth models only (no samples); pad layout and
// notes from §2.3 (Trance, Rock, Metal columns; the electronic kits use the
// default column with their own sounds, Melodic Techno with tom 2 and DnB
// with a ride on pad 9). Values are starting points for listening.
constexpr PadRow pad (Model m, int note, float level, float tune, float decay, float pan, float character, float send1 = 0.0f,
                      float send2 = 0.1f) noexcept
{
    return { m, static_cast<std::uint8_t> (note), 0, level, tune, decay, pan, character, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f,
             m == Model::tom ? 30.0f : 24.0f, m == Model::tom ? 0.05f : 0.035f, 1.6f, 0.3f, 0.25f, send1, send2 };
}
constexpr PadRow filtered (PadRow r, Filter f, float cutoff, float resonance) noexcept
{
    r.filter = f;
    r.cutoff = cutoff;
    r.resonance = resonance;
    return r;
}
constexpr PadRow body (PadRow r, float attack, float sustain, float pitchAmount, float pitchDecay, float drive, float sub, float tail) noexcept
{
    r.attack = attack;
    r.sustain = sustain;
    r.pitchAmount = pitchAmount;
    r.pitchDecay = pitchDecay;
    r.drive = drive;
    r.sub = sub;
    r.tail = tail;
    return r;
}
constexpr PadRow choked (PadRow r, int group) noexcept
{
    r.choke = static_cast<std::uint8_t> (group);
    return r;
}

struct GenreKit {
    KitTable table;
    std::array<PadRole, kit::padCount> roles;
    FactoryRelease release;
};

constexpr auto K = PadRole::kick, S = PadRole::snare, C = PadRole::clap, CH = PadRole::closedHat, OH = PadRole::openHat,
               T1 = PadRole::tom1, R = PadRole::rim, CY = PadRole::crash, P = PadRole::percussion, RD = PadRole::ride,
               T2 = PadRole::tom2, T3 = PadRole::tom3;
constexpr std::array<PadRole, kit::padCount> tranceRoles { K, S, C, CH, OH, T1, R, CY, RD, T2, T3, P, P, P, P, P };
constexpr std::array<PadRole, kit::padCount> electronicRoles { K, S, C, CH, OH, T1, R, CY, P, P, P, P, P, P, P, P };
constexpr std::array<PadRole, kit::padCount> melodicTechnoRoles { K, S, C, CH, OH, T1, R, CY, T2, P, P, P, P, P, P, P };
constexpr std::array<PadRole, kit::padCount> dnbRoles { K, S, C, CH, OH, T1, R, CY, RD, P, P, P, P, P, P, P };
constexpr std::array<PadRole, kit::padCount> rockRoles { K, S, R, CH, OH, T1, T2, T3, CY, RD, P, P, P, P, P, P };

constexpr Style styleId (int id) noexcept { return static_cast<Style> (id); }

const std::array<GenreKit, factoryKitCount - originalFactoryKitCount>& genreKits()
{
    static const std::array<GenreKit, factoryKitCount - originalFactoryKitCount> kits {
        // Trance: punchy kick with a long tail, bright clap and snare, airy hats, 3 toms, crash, ride.
        GenreKit { { { "Trance", "Trance", { styleId (7), styleId (8) }, 138.0f }, 0.337f, 3, 0.35f, 0.55f, 0.55f, 0.5f,
                     { body (pad (Model::kick, 36, 0.9f, -1.0f, 0.4f, 0.0f, 0.5f), 0.1f, 0.0f, 30.0f, 0.03f, 2.0f, 0.4f, 0.3f),
                       pad (Model::snare, 38, 0.6f, 1.0f, 0.2f, 0.0f, 0.6f, 0.0f, 0.25f),
                       pad (Model::clap, 39, 0.65f, 0.0f, 0.3f, 0.0f, 0.55f, 0.0f, 0.35f),
                       choked (filtered (pad (Model::hat, 42, 0.45f, 2.0f, 0.05f, 0.15f, 0.7f, 0.0f, 0.0f), Filter::hp12, 20000.0f, 0.1f), 1),
                       choked (pad (Model::hat, 46, 0.45f, 1.0f, 0.35f, 0.15f, 0.65f), 1),
                       pad (Model::tom, 50, 0.55f, 4.0f, 0.3f, -0.3f, 0.5f, 0.0f, 0.2f),
                       pad (Model::rim, 37, 0.5f, 3.0f, 0.06f, 0.3f, 0.6f, 0.2f, 0.0f),
                       pad (Model::cymbal, 49, 0.35f, 0.0f, 1.8f, -0.25f, 0.6f, 0.0f, 0.3f),
                       pad (Model::cymbal, 51, 0.32f, 4.0f, 0.9f, 0.25f, 0.3f),
                       pad (Model::tom, 47, 0.55f, 0.0f, 0.35f, 0.0f, 0.5f, 0.0f, 0.2f),
                       pad (Model::tom, 43, 0.55f, -5.0f, 0.4f, 0.3f, 0.5f, 0.0f, 0.2f),
                       filtered (pad (Model::hat, 82, 0.35f, 6.0f, 0.08f, 0.45f, 0.9f), Filter::bp, 20000.0f, 0.2f),
                       pad (Model::perc, 63, 0.45f, 2.0f, 0.15f, -0.4f, 0.4f, 0.15f, 0.1f),
                       filtered (pad (Model::hat, 54, 0.35f, 9.0f, 0.12f, 0.5f, 0.85f), Filter::hp24, 20000.0f, 0.0f),
                       body (pad (Model::cymbal, 60, 0.3f, -4.0f, 2.5f, 0.0f, 0.2f, 0.0f, 0.5f), 0.6f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f),
                       pad (Model::snare, 40, 0.45f, 4.0f, 0.12f, 0.0f, 0.75f, 0.0f, 0.2f) } },
                   tranceRoles, FactoryRelease::listeningPending },
        // Hard Trance: hard driven kick, tight snare and clap, hissy 16th hats.
        GenreKit { { { "Hard Trance", "Trance / Hard", { styleId (21), styleId (21) }, 150.0f }, 0.402f, 2, 0.3f, 0.6f, 0.45f, 0.6f,
                     { body (pad (Model::kick, 36, 0.92f, 0.0f, 0.35f, 0.0f, 0.65f), 0.2f, 0.0f, 40.0f, 0.025f, 3.0f, 0.35f, 0.25f),
                       pad (Model::snare, 38, 0.6f, 2.0f, 0.18f, 0.0f, 0.7f, 0.0f, 0.2f),
                       pad (Model::clap, 39, 0.65f, 1.0f, 0.25f, 0.0f, 0.65f, 0.0f, 0.3f),
                       choked (filtered (pad (Model::hat, 42, 0.45f, 4.0f, 0.04f, 0.2f, 0.8f, 0.0f, 0.0f), Filter::hp24, 20000.0f, 0.0f), 1),
                       choked (pad (Model::hat, 46, 0.45f, 3.0f, 0.3f, 0.2f, 0.75f), 1),
                       pad (Model::tom, 50, 0.55f, 5.0f, 0.25f, -0.3f, 0.6f, 0.0f, 0.15f),
                       pad (Model::rim, 37, 0.5f, 4.0f, 0.05f, 0.3f, 0.7f, 0.2f, 0.0f),
                       pad (Model::cymbal, 49, 0.35f, 2.0f, 1.5f, -0.25f, 0.7f, 0.0f, 0.25f),
                       pad (Model::cymbal, 51, 0.32f, 6.0f, 0.7f, 0.25f, 0.35f),
                       pad (Model::tom, 47, 0.55f, 1.0f, 0.3f, 0.0f, 0.6f, 0.0f, 0.15f),
                       pad (Model::tom, 43, 0.55f, -4.0f, 0.35f, 0.3f, 0.6f, 0.0f, 0.15f),
                       filtered (pad (Model::hat, 82, 0.35f, 7.0f, 0.06f, 0.45f, 0.9f), Filter::bp, 20000.0f, 0.2f),
                       pad (Model::perc, 63, 0.45f, 4.0f, 0.12f, -0.4f, 0.5f, 0.15f, 0.1f),
                       filtered (pad (Model::hat, 54, 0.35f, 10.0f, 0.1f, 0.5f, 0.85f), Filter::hp24, 20000.0f, 0.0f),
                       pad (Model::cymbal, 60, 0.3f, -2.0f, 2.0f, 0.0f, 0.3f, 0.0f, 0.45f),
                       pad (Model::snare, 40, 0.45f, 5.0f, 0.1f, 0.0f, 0.8f, 0.0f, 0.15f) } },
                   tranceRoles, FactoryRelease::listeningPending },
        // Dark Psy: short dry kick, metallic hats, glitchy percussion.
        GenreKit { { { "Dark Psy", "Psy / Dark", { styleId (9), styleId (9) }, 148.0f }, 0.562f, 3, 0.4f, 0.4f, 0.4f, 0.65f,
                     { body (pad (Model::kick, 36, 0.9f, 1.0f, 0.22f, 0.0f, 0.6f), 0.2f, -0.1f, 38.0f, 0.018f, 2.4f, 0.2f, 0.15f),
                       pad (Model::snare, 38, 0.55f, 3.0f, 0.12f, 0.0f, 0.75f, 0.0f, 0.2f),
                       pad (Model::clap, 39, 0.55f, 2.0f, 0.18f, -0.1f, 0.7f, 0.1f, 0.25f),
                       choked (filtered (pad (Model::hat, 42, 0.42f, 5.0f, 0.035f, 0.2f, 0.85f, 0.0f, 0.0f), Filter::hp24, 20000.0f, 0.0f), 1),
                       choked (pad (Model::hat, 46, 0.42f, 4.0f, 0.22f, 0.2f, 0.8f), 1),
                       pad (Model::tom, 45, 0.5f, -3.0f, 0.25f, -0.3f, 0.6f, 0.2f, 0.1f),
                       pad (Model::rim, 37, 0.5f, 6.0f, 0.04f, 0.3f, 0.8f, 0.3f, 0.0f),
                       pad (Model::cymbal, 49, 0.32f, 3.0f, 1.2f, -0.3f, 0.75f, 0.0f, 0.3f),
                       filtered (pad (Model::perc, 50, 0.45f, 9.0f, 0.06f, -0.4f, 0.8f, 0.3f, 0.0f), Filter::bp, 3000.0f, 0.5f),
                       filtered (pad (Model::perc, 51, 0.45f, 14.0f, 0.04f, 0.4f, 0.9f, 0.3f, 0.0f), Filter::hp12, 2000.0f, 0.3f),
                       pad (Model::rim, 52, 0.45f, 10.0f, 0.03f, -0.5f, 0.9f, 0.25f, 0.05f),
                       filtered (pad (Model::hat, 53, 0.4f, 12.0f, 0.02f, 0.5f, 0.95f), Filter::bp, 6000.0f, 0.6f),
                       filtered (pad (Model::perc, 54, 0.45f, -6.0f, 0.12f, 0.3f, 0.6f, 0.2f, 0.1f), Filter::lp12, 1500.0f, 0.6f),
                       pad (Model::tom, 55, 0.45f, 8.0f, 0.08f, -0.3f, 0.8f, 0.3f, 0.0f),
                       filtered (pad (Model::perc, 56, 0.4f, 18.0f, 0.03f, 0.6f, 0.95f, 0.35f, 0.0f), Filter::hp24, 4000.0f, 0.2f),
                       pad (Model::cymbal, 57, 0.3f, 8.0f, 0.4f, -0.6f, 0.9f, 0.0f, 0.3f) } },
                   electronicRoles, FactoryRelease::listeningPending },
        // Tech House: round kick, clap and snare, shuffling hats, bongo / conga / shaker percussion.
        GenreKit { { { "Tech House", "House / Tech", { styleId (15), styleId (15) }, 125.0f }, 0.386f, 2, 0.35f, 0.45f, 0.45f, 0.55f,
                     { body (pad (Model::kick, 36, 0.9f, -2.0f, 0.42f, 0.0f, 0.4f), 0.0f, 0.2f, 22.0f, 0.045f, 1.6f, 0.5f, 0.35f),
                       pad (Model::snare, 38, 0.5f, 0.0f, 0.18f, 0.0f, 0.5f, 0.0f, 0.25f),
                       pad (Model::clap, 39, 0.65f, -1.0f, 0.28f, 0.0f, 0.45f, 0.0f, 0.3f),
                       choked (filtered (pad (Model::hat, 42, 0.45f, 0.0f, 0.06f, 0.15f, 0.55f, 0.0f, 0.05f), Filter::hp12, 20000.0f, 0.0f), 1),
                       choked (pad (Model::hat, 46, 0.48f, -1.0f, 0.38f, 0.15f, 0.5f, 0.0f, 0.15f), 1),
                       pad (Model::tom, 45, 0.5f, -6.0f, 0.4f, -0.2f, 0.35f, 0.0f, 0.2f),
                       pad (Model::rim, 37, 0.5f, 1.0f, 0.06f, 0.35f, 0.5f, 0.3f, 0.1f),
                       pad (Model::cymbal, 49, 0.32f, -2.0f, 2.0f, -0.2f, 0.5f, 0.0f, 0.3f),
                       pad (Model::perc, 50, 0.5f, 7.0f, 0.1f, -0.35f, 0.45f, 0.2f, 0.1f),
                       pad (Model::perc, 51, 0.5f, 3.0f, 0.12f, 0.35f, 0.4f, 0.2f, 0.1f),
                       pad (Model::perc, 52, 0.5f, -2.0f, 0.18f, -0.45f, 0.35f, 0.2f, 0.1f),
                       filtered (pad (Model::hat, 53, 0.38f, 6.0f, 0.07f, 0.5f, 0.9f), Filter::bp, 20000.0f, 0.2f),
                       pad (Model::perc, 54, 0.42f, 12.0f, 0.05f, 0.3f, 0.7f, 0.25f, 0.0f),
                       pad (Model::rim, 55, 0.45f, 5.0f, 0.05f, -0.5f, 0.7f, 0.35f, 0.0f),
                       filtered (pad (Model::hat, 56, 0.38f, 8.0f, 0.12f, 0.55f, 0.85f), Filter::hp24, 20000.0f, 0.0f),
                       pad (Model::cymbal, 57, 0.3f, 3.0f, 0.8f, 0.0f, 0.6f, 0.0f, 0.4f) } },
                   electronicRoles, FactoryRelease::listeningPending },
        // Melodic Techno: deep kick, soft clap, two tuned toms, tonal percussion with space.
        GenreKit { { { "Melodic Techno", "Techno / Melodic", { styleId (18), styleId (18) }, 122.0f }, 0.375f, 4, 0.45f, 0.4f, 0.65f, 0.45f,
                     { body (pad (Model::kick, 36, 0.9f, -3.0f, 0.5f, 0.0f, 0.35f), 0.0f, 0.3f, 18.0f, 0.06f, 1.5f, 0.6f, 0.45f),
                       pad (Model::snare, 38, 0.5f, -2.0f, 0.22f, 0.0f, 0.4f, 0.0f, 0.35f),
                       pad (Model::clap, 39, 0.55f, -2.0f, 0.35f, 0.0f, 0.35f, 0.1f, 0.45f),
                       choked (filtered (pad (Model::hat, 42, 0.4f, -1.0f, 0.07f, 0.2f, 0.45f, 0.0f, 0.1f), Filter::hp12, 20000.0f, 0.0f), 1),
                       choked (pad (Model::hat, 46, 0.42f, -2.0f, 0.45f, 0.2f, 0.4f, 0.0f, 0.2f), 1),
                       pad (Model::tom, 45, 0.5f, -2.0f, 0.5f, -0.3f, 0.3f, 0.2f, 0.3f),
                       pad (Model::rim, 37, 0.45f, 0.0f, 0.07f, 0.35f, 0.4f, 0.35f, 0.2f),
                       pad (Model::cymbal, 49, 0.3f, -4.0f, 2.6f, -0.2f, 0.4f, 0.0f, 0.45f),
                       pad (Model::tom, 47, 0.5f, 3.0f, 0.45f, 0.3f, 0.3f, 0.2f, 0.3f),
                       pad (Model::perc, 50, 0.45f, 7.0f, 0.3f, -0.4f, 0.2f, 0.4f, 0.3f),
                       pad (Model::perc, 51, 0.45f, 10.0f, 0.25f, 0.4f, 0.2f, 0.4f, 0.3f),
                       pad (Model::perc, 52, 0.45f, 12.0f, 0.2f, -0.5f, 0.25f, 0.4f, 0.3f),
                       filtered (pad (Model::perc, 53, 0.42f, 5.0f, 0.35f, 0.5f, 0.2f, 0.3f, 0.35f), Filter::lp12, 3000.0f, 0.4f),
                       pad (Model::rim, 54, 0.4f, 9.0f, 0.06f, -0.3f, 0.5f, 0.4f, 0.2f),
                       filtered (pad (Model::hat, 55, 0.35f, 3.0f, 0.15f, 0.6f, 0.6f, 0.2f, 0.3f), Filter::bp, 20000.0f, 0.3f),
                       pad (Model::cymbal, 56, 0.28f, 2.0f, 1.2f, 0.0f, 0.3f, 0.0f, 0.5f) } },
                   melodicTechnoRoles, FactoryRelease::listeningPending },
        // DnB: punchy kick, cracking snare, ghost snare, fast hats, ride, crash.
        GenreKit { { { "DnB", "Drum & Bass", { styleId (16), styleId (17) }, 174.0f }, 0.406f, 3, 0.3f, 0.5f, 0.5f, 0.5f,
                     { body (pad (Model::kick, 36, 0.9f, 1.0f, 0.3f, 0.0f, 0.55f), 0.25f, 0.0f, 34.0f, 0.02f, 2.0f, 0.35f, 0.2f),
                       body (pad (Model::snare, 38, 0.7f, 4.0f, 0.22f, 0.0f, 0.75f, 0.0f, 0.25f), 0.3f, 0.0f, 28.0f, 0.03f, 2.0f, 0.3f, 0.3f),
                       pad (Model::clap, 39, 0.55f, 1.0f, 0.25f, 0.0f, 0.6f, 0.0f, 0.3f),
                       choked (filtered (pad (Model::hat, 42, 0.42f, 5.0f, 0.035f, 0.15f, 0.8f, 0.0f, 0.0f), Filter::hp24, 20000.0f, 0.0f), 1),
                       choked (pad (Model::hat, 46, 0.45f, 4.0f, 0.3f, 0.15f, 0.7f), 1),
                       pad (Model::tom, 45, 0.55f, 0.0f, 0.3f, -0.3f, 0.5f, 0.0f, 0.2f),
                       pad (Model::rim, 37, 0.5f, 5.0f, 0.05f, 0.3f, 0.7f, 0.2f, 0.05f),
                       pad (Model::cymbal, 49, 0.33f, 1.0f, 1.6f, -0.25f, 0.65f, 0.0f, 0.3f),
                       pad (Model::cymbal, 51, 0.3f, 5.0f, 0.8f, 0.3f, 0.35f, 0.0f, 0.15f),
                       pad (Model::snare, 50, 0.4f, 6.0f, 0.1f, 0.1f, 0.8f, 0.0f, 0.15f),
                       pad (Model::perc, 51, 0.45f, 8.0f, 0.08f, 0.4f, 0.6f, 0.2f, 0.05f),
                       filtered (pad (Model::hat, 52, 0.35f, 7.0f, 0.06f, -0.45f, 0.9f), Filter::bp, 20000.0f, 0.2f),
                       pad (Model::perc, 53, 0.45f, 2.0f, 0.15f, -0.4f, 0.45f, 0.15f, 0.1f),
                       pad (Model::rim, 54, 0.42f, 9.0f, 0.04f, 0.5f, 0.8f, 0.3f, 0.0f),
                       filtered (pad (Model::hat, 55, 0.35f, 11.0f, 0.03f, -0.55f, 0.9f), Filter::hp24, 20000.0f, 0.0f),
                       pad (Model::cymbal, 56, 0.28f, 6.0f, 0.5f, 0.0f, 0.8f, 0.0f, 0.4f) } },
                   dnbRoles, FactoryRelease::listeningPending },
        // Rock (acoustic layout from the synth models; NOT RELEASE-READY without licensed samples).
        GenreKit { { { "Rock (synth sketch)", "Rock", { styleId (11), styleId (11) }, 125.0f }, 0.328f, 3, 0.25f, 0.4f, 0.55f, 0.45f,
                     { body (pad (Model::kick, 36, 0.9f, -1.0f, 0.35f, 0.0f, 0.4f), 0.3f, 0.1f, 14.0f, 0.04f, 1.4f, 0.4f, 0.3f),
                       body (pad (Model::snare, 38, 0.7f, 0.0f, 0.28f, 0.0f, 0.5f, 0.0f, 0.3f), 0.2f, 0.1f, 18.0f, 0.04f, 1.6f, 0.3f, 0.35f),
                       pad (Model::rim, 37, 0.5f, 0.0f, 0.08f, 0.1f, 0.4f, 0.0f, 0.2f),
                       choked (pad (Model::hat, 42, 0.45f, 0.0f, 0.07f, 0.25f, 0.5f, 0.0f, 0.1f), 1),
                       choked (pad (Model::hat, 46, 0.45f, -1.0f, 0.5f, 0.25f, 0.45f, 0.0f, 0.15f), 1),
                       pad (Model::tom, 48, 0.6f, 4.0f, 0.4f, 0.2f, 0.35f, 0.0f, 0.25f),
                       pad (Model::tom, 45, 0.6f, 0.0f, 0.45f, -0.1f, 0.35f, 0.0f, 0.25f),
                       pad (Model::tom, 43, 0.6f, -5.0f, 0.55f, -0.3f, 0.35f, 0.0f, 0.25f),
                       pad (Model::cymbal, 49, 0.38f, -1.0f, 2.2f, -0.35f, 0.5f, 0.0f, 0.3f),
                       pad (Model::cymbal, 51, 0.34f, 3.0f, 1.4f, 0.35f, 0.3f, 0.0f, 0.2f),
                       pad (Model::cymbal, 57, 0.36f, 1.0f, 2.0f, 0.45f, 0.55f, 0.0f, 0.3f),
                       pad (Model::cymbal, 53, 0.32f, 8.0f, 0.6f, 0.35f, 0.2f, 0.0f, 0.15f),
                       pad (Model::cymbal, 55, 0.32f, 6.0f, 0.7f, -0.45f, 0.6f, 0.0f, 0.25f),
                       choked (pad (Model::hat, 44, 0.4f, -1.0f, 0.06f, 0.25f, 0.4f, 0.0f, 0.05f), 1),
                       pad (Model::perc, 56, 0.4f, 6.0f, 0.1f, 0.3f, 0.6f, 0.0f, 0.1f),
                       filtered (pad (Model::hat, 54, 0.35f, 9.0f, 0.12f, -0.5f, 0.85f), Filter::hp24, 20000.0f, 0.0f) } },
                   rockRoles, FactoryRelease::needsLicensedSamples },
        // Metal (acoustic layout from the synth models; NOT RELEASE-READY without licensed samples).
        GenreKit { { { "Metal (synth sketch)", "Metal", { styleId (12), styleId (13) }, 160.0f }, 0.208f, 3, 0.2f, 0.45f, 0.5f, 0.5f,
                     { body (pad (Model::kick, 36, 0.9f, 2.0f, 0.2f, 0.0f, 0.75f), 0.5f, 0.0f, 20.0f, 0.015f, 1.8f, 0.25f, 0.1f),
                       body (pad (Model::snare, 38, 0.72f, 2.0f, 0.24f, 0.0f, 0.65f, 0.0f, 0.3f), 0.3f, 0.05f, 22.0f, 0.03f, 2.0f, 0.3f, 0.3f),
                       pad (Model::rim, 37, 0.5f, 2.0f, 0.06f, 0.1f, 0.5f, 0.0f, 0.15f),
                       choked (pad (Model::hat, 42, 0.45f, 1.0f, 0.05f, 0.25f, 0.6f, 0.0f, 0.05f), 1),
                       choked (pad (Model::hat, 46, 0.45f, 0.0f, 0.45f, 0.25f, 0.55f, 0.0f, 0.1f), 1),
                       pad (Model::tom, 48, 0.62f, 5.0f, 0.35f, 0.25f, 0.45f, 0.0f, 0.2f),
                       pad (Model::tom, 45, 0.62f, 1.0f, 0.4f, -0.05f, 0.45f, 0.0f, 0.2f),
                       pad (Model::tom, 41, 0.62f, -6.0f, 0.5f, -0.35f, 0.45f, 0.0f, 0.2f),
                       pad (Model::cymbal, 49, 0.38f, 0.0f, 2.0f, -0.4f, 0.6f, 0.0f, 0.25f),
                       pad (Model::cymbal, 51, 0.34f, 4.0f, 1.2f, 0.4f, 0.35f, 0.0f, 0.15f),
                       pad (Model::cymbal, 57, 0.36f, 2.0f, 1.8f, 0.5f, 0.6f, 0.0f, 0.25f),
                       pad (Model::cymbal, 52, 0.36f, -3.0f, 1.4f, -0.5f, 0.9f, 0.0f, 0.25f),
                       pad (Model::cymbal, 55, 0.32f, 7.0f, 0.6f, 0.3f, 0.65f, 0.0f, 0.2f),
                       pad (Model::cymbal, 53, 0.32f, 9.0f, 0.5f, 0.4f, 0.25f, 0.0f, 0.1f),
                       choked (pad (Model::hat, 44, 0.4f, 0.0f, 0.05f, 0.25f, 0.5f, 0.0f, 0.05f), 1),
                       pad (Model::snare, 40, 0.5f, 5.0f, 0.15f, 0.0f, 0.8f, 0.0f, 0.2f) } },
                   rockRoles, FactoryRelease::needsLicensedSamples },
    };
    return kits;
}

const KitTable& table (int kit) noexcept
{
    kit = std::clamp (kit, 0, factoryKitCount - 1);
    return kit < originalFactoryKitCount ? tables[kit] : genreKits()[static_cast<std::size_t> (kit - originalFactoryKitCount)].table;
}

std::string sha (const void* data, std::size_t size)
{
    return juce::SHA256 (data, size).toHexString().toStdString();
}

} // namespace

const FactoryKitInfo& factoryKitInfo (int kit) noexcept
{
    return table (kit).info;
}

FactoryRelease factoryKitRelease (int kit) noexcept
{
    return kit >= originalFactoryKitCount && kit < factoryKitCount ? genreKits()[static_cast<std::size_t> (kit - originalFactoryKitCount)].release
                                                                   : FactoryRelease::listeningPending;
}

const char* factoryReleaseText (FactoryRelease r) noexcept
{
    return r == FactoryRelease::needsLicensedSamples ? "NOT-RELEASE-READY:needs-licensed-samples" : "listening-pending";
}

PadRoles factoryKitRoles (int kit)
{
    PadRoles roles;
    if (kit >= originalFactoryKitCount && kit < factoryKitCount)
    {
        roles.explicitRoles = true;
        roles.role = genreKits()[static_cast<std::size_t> (kit - originalFactoryKitCount)].roles;
    }
    return roles;
}

int factoryKitOfStyle (int style) noexcept
{
    // Registry ids 0-22 (StyleRegistry) -> factory kit (PR 137 §5).
    static constexpr int kits[factoryStyleCount] = {
        0, 0, 1, 1, 2, 2, 1,   // Full-on, Goa, House, Techno, Trap, Breaks, Ambient
        3, 3, 5, 3,            // Uplifting, Anthem, Dark Psy, Melodic Trance
        9, 10, 10, 10,         // Hard Rock, Heavy, Symphonic, Nordic Metal
        6, 8, 8, 7,            // Tech House, DnB, Melodic DnB, Melodic Techno
        3, 3, 4, 3             // Progressive, Dream, Hard, Symphonic Trance
    };
    return style >= 0 && style < factoryStyleCount ? kits[style] : -1;
}

GeneratorSettings factoryStyleGenerator (int style) noexcept
{
    GeneratorSettings g;
    g.style = static_cast<Style> (std::clamp (style, 0, factoryStyleCount - 1));
    if (const auto defaults = styleDefaults (g))
        setGeneratorValues (g, *defaults);
    g.seed = 50000u + 101u * static_cast<std::uint32_t> (std::clamp (style, 0, factoryStyleCount - 1));
    return g;
}

bool factoryStyleSong (int style, PatternStore::Everything& out)
{
    if (style < 0 || style >= factoryStyleCount)
        return false;
    for (int slot = 0; slot < PatternStore::slotCount; ++slot)
    {
        out.slots[static_cast<std::size_t> (slot)] = {};   // 4/4 at 16ths, 4 bars
        out.alternates[static_cast<std::size_t> (slot)] = {};
    }
    out.song = SongData::initial();
    out.song.length = 32;
    out.song.loopStart = 0;
    out.song.loopLength = 32;
    return generateSong (factoryStyleGenerator (style), out, RoleMap::from (factoryKitRoles (factoryKitOfStyle (style))));
}

kit::KitSettings factoryKit (int index)
{
    const auto& t = table (index);
    kit::KitSettings k = defaultKit();
    k.volume = t.volume;
    for (int i = 0; i < kit::padCount; ++i)
    {
        const auto& r = t.pads[i];
        kit::PadSettings p;
        p.model = r.model;
        p.note = r.note;
        p.chokeGroup = r.choke;
        p.polyphony = r.model == Model::cymbal || r.model == Model::tom ? 2 : 1;
        p.level = r.level;
        p.tune = r.tune;
        p.decay = r.decay;
        p.pan = r.pan;
        p.character = r.character;
        p.cutoff = r.cutoff;
        p.filter = r.filter;
        p.resonance = r.resonance;
        p.attack = r.attack;
        p.sustain = r.sustain;
        p.pitchAmount = r.pitchAmount;
        p.pitchDecay = r.pitchDecay;
        p.drive = r.drive;
        p.sub = r.sub;
        p.tail = r.tail;
        p.send1 = r.send1;
        p.send2 = r.send2;
        k.pads[static_cast<std::size_t> (i)] = p;
    }
    return k;
}

std::map<std::string, float> factoryKitParameters (int index)
{
    const auto& t = table (index);
    std::map<std::string, float> v;
    for (int i = 0; i < kit::padCount; ++i)
    {
        const auto prefix = padPrefix (i);
        const auto& r = t.pads[i];
        v[prefix + ".level"] = r.level;
        v[prefix + ".tune"] = r.tune;
        v[prefix + ".decay"] = r.decay;
        v[prefix + ".pan"] = r.pan;
        v[prefix + ".cutoff"] = r.cutoff;
        v[prefix + ".send1"] = r.send1;
        v[prefix + ".send2"] = r.send2;
    }
    v["drm.kit.volume"] = t.volume;
    v["drm.fx.delay.time"] = static_cast<float> (t.delayTime);
    v["drm.fx.delay.feedback"] = t.delayFeedback;
    v["drm.fx.delay.tone"] = t.delayTone;
    v["drm.fx.delay.level"] = 0.8f;
    v["drm.fx.reverb.size"] = t.reverbSize;
    v["drm.fx.reverb.damp"] = t.reverbDamp;
    v["drm.fx.reverb.level"] = 0.8f;
    return v;
}

GeneratorSettings factoryGenerator (int kit, int slot) noexcept
{
    const auto& info = table (kit).info;
    GeneratorSettings g;
    // Slots 1-3 in the family's first style, 4-5 in its second.
    g.style = info.styles[slot < 3 ? 0 : 1];
    static constexpr float density[5] = { 0.45f, 0.6f, 0.8f, 0.5f, 0.75f };
    static constexpr float complexity[5] = { 0.25f, 0.4f, 0.6f, 0.45f, 0.65f };
    static constexpr float syncopation[5] = { 0.2f, 0.35f, 0.5f, 0.4f, 0.55f };
    g.density = density[std::clamp (slot, 0, 4)];
    g.complexity = complexity[std::clamp (slot, 0, 4)];
    g.syncopation = syncopation[std::clamp (slot, 0, 4)];
    g.groove = 0.5f;
    g.swing = g.style == Style::house ? 0.25f : g.style == Style::trap || g.style == Style::breaks ? 0.12f : 0.0f;
    g.humanize = g.style == Style::breaks ? 0.35f : 0.15f;
    g.fills = 0.5f;
    g.variation = 0.4f;
    g.seed = 1000u * static_cast<std::uint32_t> (kit + 1) + static_cast<std::uint32_t> (slot) * 17u + 1u;
    return g;
}

void factoryPatterns (int kit, PatternStore::Everything& out)
{
    if (kit >= originalFactoryKitCount)
    {
        factoryStyleSong (static_cast<int> (table (kit).info.styles[0]), out);
        return;
    }
    for (int slot = 0; slot < slotCountForSong; ++slot)
    {
        auto& a = out.slots[static_cast<std::size_t> (slot)];
        a = {};   // 4/4 at 16ths, 4 bars
        const auto g = factoryGenerator (kit, slot);
        generatePattern (g, a);
        auto& b = out.alternates[static_cast<std::size_t> (slot)];
        b = {};
        generateVariation (g, a, b);
    }
    // A 16-bar song: slot 2 with its B, then slot 5 with its B.
    out.song = {};
    out.song.length = 16;
    out.song.loopLength = 16;
    out.song.place (0, 1, 0, 4);
    out.song.place (4, 1, 1, 4);
    out.song.place (8, 4, 0, 4);
    out.song.place (12, 4, 1, 4);
}

std::string factoryManifest()
{
    std::ostringstream out;
    out << "item,kind,sha256,release\n";
    const auto all = std::make_unique<PatternStore::Everything>();
    for (int kit = 0; kit < factoryKitCount; ++kit)
    {
        const auto name = std::string (factoryKitInfo (kit).name);
        const auto release = std::string (factoryReleaseText (factoryKitRelease (kit)));
        {
            // The kit: its DPAD section followed by its parameter values in ID
            // order, then its DROL section when it has roles (genre kits).
            auto bytes = KitStore::encode (factoryKit (kit)).data;
            for (const auto& [id, value] : factoryKitParameters (kit))
            {
                for (const char c : id)
                    bytes.push_back (static_cast<std::byte> (c));
                std::uint32_t bits = 0;
                std::memcpy (&bits, &value, sizeof bits);
                for (int shift = 0; shift < 32; shift += 8)
                    bytes.push_back (static_cast<std::byte> ((bits >> shift) & 0xffu));
            }
            if (const auto roles = factoryKitRoles (kit); roles.explicitRoles)
            {
                const auto drol = encodeRoles (roles).data;
                bytes.insert (bytes.end(), drol.begin(), drol.end());
            }
            out << name << ",kit," << sha (bytes.data(), bytes.size()) << "," << release << "\n";
        }
        if (kit >= originalFactoryKitCount)
            continue;   // a genre kit's content is the song presets of its styles
        factoryPatterns (kit, *all);
        const auto section = PatternStore::encode (all->slots, all->alternates);
        out << name << ",patterns," << sha (section.data.data(), section.data.size()) << "," << release << "\n";
        const auto song = encodeSong (all->song);
        out << name << ",song," << sha (song.data.data(), song.data.size()) << "," << release << "\n";
    }
    // One song preset per style (PR 137 §5), with the release status of its kit.
    for (int style = 0; style < factoryStyleCount; ++style)
    {
        const auto name = std::string (styleName (static_cast<Style> (style))) + " song";
        const auto release = std::string (factoryReleaseText (factoryKitRelease (factoryKitOfStyle (style))));
        factoryStyleSong (style, *all);
        const auto section = PatternStore::encode (all->slots, all->alternates);
        out << name << ",patterns," << sha (section.data.data(), section.data.size()) << "," << release << "\n";
        const auto song = encodeSong (all->song);
        out << name << ",song," << sha (song.data.data(), song.data.size()) << "," << release << "\n";
    }
    return out.str();
}

} // namespace vox::drums
