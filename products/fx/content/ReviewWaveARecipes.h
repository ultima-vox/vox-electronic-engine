#pragma once
// Included by FactoryCandidates.h inside content::detail. These are authored
// arrangements, not a generated variation bank. Pitch and pan values are the
// engine's normalized trajectory values, not MIDI notes or absolute percent.
struct WaveLayer {
    F family = F::riser;
    float gain=0,pan=0,start=0,span=1,transpose=0,character=0;
    source::Filter filter{};
};
struct Contour { float start, middle, end, pivot; };
struct WaveHit { std::uint8_t step=0, note=0, velocity=0, chance=100, ratchet=1; };
struct WaveBeat {
    pattern::Grid grid=pattern::Grid::sixteenth;
    std::uint8_t bars=1;
    float swing=0;
    std::array<WaveHit,16> hits{};
};
struct WaveItem {
    Entry entry;
    std::array<WaveLayer,4> layers;
    Contour amp;
    Contour pitch{.5f,.5f,.5f,.5f};
    Contour pan{.5f,.5f,.5f,.5f};
    std::array<effects::Settings,4> fx{};
    source::Tonality key{};
    WaveBeat beat{};
};
using R=Role;
using M=PlayMode;
using G=pattern::Grid;
inline constexpr WaveItem waveA[]{
    // Impacts — reverse-flare downbeat, short PA sub, and a wide non-tonal crack.
    {{"voxfx.impact.crystal-bulkhead","Crystal Bulkhead","Progressive Tech","Brittle",R::impact,M::oneShot,true,45,0,1.9f,132,1,false,0,"Reverse-flare downbeat"},
     {{{F::riser,.69f,-.35f,0,.31f,0,-.46f,{T::highpass,470,.7f,2}},{F::impact,.95f,0,.28f,.59f,-12,.27f,{T::lowpass,3100,.71f,2}},
       {F::metallic,.64f,.36f,.27f,.53f,7,.37f,{T::bandpass,2150,.85f,2}},{F::granular,.37f,-.3f,.56f,.44f,0,-.26f,{T::lowpass,4400,.71f,2}}}},
     {.23f,1,0,.32f},{.5f,.61f,.39f,.3f},{.37f,.52f,.65f,.26f},{{{E::reverb,.19f,.41f},{E::lowpass,.15f,.58f}}},{K::minor,9}},
    {{"voxfx.impact.substation","Substation","Hi-Tech","Dense",R::impact,M::oneShot,true,34,0,.42f,178,1,false,0,"Short kick-adjacent sub drop"},
     {{{F::impact,1,0,0,.94f,-12,-.66f,{T::lowpass,275,.72f,4}},{F::downlifter,.53f,0,0,.55f,-7,.18f,{T::lowpass,580,.81f,2}},
       {F::glitch,.29f,.06f,0,.18f,0,.2f,{T::highpass,1700,.61f,2}}}},
     {1,.6f,0,.27f},{.65f,.39f,.36f,.18f},{.5f,.5f,.5f,.5f},{{{E::drive,.38f,.57f}}}},
    {{"voxfx.impact.curtain-crack","Curtain Crack","Uplifting","Sharp",R::impact,M::oneShot,false,60,0,.83f,138,1,false,0,"Non-tonal downbeat accent"},
     {{{F::riser,.95f,-.3f,0,.18f,0,.77f,{T::highpass,1890,.72f,2}},{F::riser,.68f,.43f,.018f,.36f,0,-.55f,{T::bandpass,2850,1.f,2}},
       {F::riser,.72f,0,.03f,.9f,0,-.16f,{T::lowpass,720,.71f,4}}}},
     {.95f,.44f,0,.14f},{.5f,.5f,.5f,.5f},{.63f,.49f,.34f,.28f},{{{E::reverb,.31f,.44f}}}},
    // Risers — four-bar countdown, short Goa flare, and a sixteen-bar organ lift.
    {{"voxfx.riser.signal-flags","Signal Flags","Daylight Trance","Hopeful",R::riser,M::transition,true,65,4,2,136,1,false,0,"Four-bar countdown into a major drop"},
     {{{F::texture,.73f,-.38f,0,.97f,0,.46f,{T::highpass,116,.73f,2}},{F::formant,.61f,.42f,.23f,.77f,7,.31f,{T::bandpass,1750,.88f,2}},
       {F::laser,.35f,-.22f,.71f,.287f,12,-.54f,{T::lowpass,5400,.76f,2}},{F::impact,.55f,0,.982f,.018f,-12,.28f,{}}}},
     {.07f,.55f,.95f,.65f},{.42f,.49f,.62f,.76f},{.33f,.64f,.5f,.74f},{{{E::chorus,.29f,.34f},{E::reverb,.17f,.5f}}},{K::major,5}},
    {{"voxfx.riser.saffron-spark","Saffron Spark","Goa Psy","Acidic",R::riser,M::gate,true,56,0,1.4f,146,1,false,0,"Short resonant lead-in for a fill"},
     {{{F::squelch,.76f,.18f,0,.95f,-5,.49f,{T::lowpass,2700,1.2f,2}},{F::riser,.57f,-.33f,.19f,.81f,0,.21f,{T::highpass,860,.76f,2}},
       {F::metallic,.43f,.37f,.6f,.4f,12,.54f,{T::bandpass,1400,.74f,2}}}},
     {.05f,.84f,.37f,.8f},{.3f,.47f,.74f,.63f},{.58f,.32f,.64f,.5f},{{{E::drive,.35f,.54f},{E::phaser,.16f,.25f}}},{K::phrygian,8}},
    {{"voxfx.riser.alloy-sunrise","Alloy Sunrise","Uplifting","Expansive",R::riser,M::transition,true,62,16,4,144,1,false,0,"Sixteen-bar harmonic build"},
     {{{F::texture,.67f,-.29f,0,1,-12,.61f,{T::lowpass,1810,.71f,2}},{F::shepard,.79f,.18f,.08f,.92f,0,-.41f,{T::highpass,130,.71f,2}},
       {F::formant,.52f,.42f,.41f,.59f,7,.3f,{T::bandpass,2400,.77f,2}},{F::riser,.62f,-.43f,.74f,.26f,0,-.48f,{T::highpass,2300,.78f,2}}}},
     {.11f,.64f,1,.72f},{.41f,.54f,.62f,.64f},{.43f,.61f,.53f,.56f},{{{E::chorus,.17f,.47f},{E::reverb,.25f,.66f}}},{K::major,2}},
    // Falls — modal cascade, filtered air release, and a two-bar descending chord.
    {{"voxfx.fall.quarry-glass","Quarry Glass","Forest Psy","Jagged",R::fall,M::oneShot,true,55,0,2.9f,152,1,false,0,"Tumbling modal cascade after the drop"},
     {{{F::metallic,.83f,-.34f,0,.59f,7,-.37f,{T::bandpass,1190,1.12f,2}},{F::downlifter,.74f,.08f,.08f,.82f,-7,.18f,{T::lowpass,3700,.75f,2}},
       {F::glitch,.56f,.43f,.23f,.61f,0,.48f,{T::notch,710,.8f,2}}}},
     {.96f,.72f,0,.38f},{.77f,.48f,.26f,.35f},{.24f,.71f,.53f,.63f},{{{E::bitcrush,.12f,.47f},{E::delay,.16f,.15f}}},{K::dorian,7}},
    {{"voxfx.fall.decompression","Decompression","Progressive Tech","Dry",R::fall,M::oneShot,false,60,0,1.18f,130,1,false,0,"Dry filtered pressure release"},
     {{{F::riser,.93f,.17f,0,1,0,-.49f,{T::lowpass,1130,.65f,4}},{F::riser,.74f,-.22f,0,.38f,0,.67f,{T::highpass,4100,.77f,2}}}},
     {1,.7f,0,.21f},{.5f,.5f,.5f,.5f},{.55f,.36f,.49f,.71f},{{{E::lowpass,.68f,.4f}}}},
    {{"voxfx.fall.iris-fold","Iris Fold","Daylight Trance","Soft",R::fall,M::transition,true,69,2,3.1f,132,1,false,0,"Two-bar minor-chord release"},
     {{{F::downlifter,.61f,-.41f,0,.98f,0,-.54f,{T::lowpass,1800,.71f,2}},{F::formant,.52f,.28f,0,.87f,-5,-.37f,{T::lowpass,2300,.7f,2}},
       {F::texture,.59f,.09f,.18f,.82f,-12,.56f,{T::highpass,98,.71f,2}}}},
     {.68f,.86f,0,.27f},{.58f,.49f,.32f,.67f},{.43f,.6f,.47f,.51f},{{{E::chorus,.25f,.55f},{E::reverb,.27f,.62f}}},{K::minor,9}},
    // Sweeps — near/far pitch arc, repeated filtered loop, and nasal stereo flyby.
    {{"voxfx.sweep.rail-camera","Rail Camera","Progressive Tech","Focused",R::sweep,M::oneShot,true,53,0,1.65f,132,1,false,0,"Left-to-right midrange flyby"},
     {{{F::whoosh,.92f,0,0,1,0,.68f,{T::bandpass,740,.85f,2}},{F::metallic,.49f,0,.24f,.48f,7,-.54f,{T::notch,1110,.77f,2}},
       {F::riser,.34f,.36f,.49f,.49f,0,-.19f,{T::highpass,1640,.62f,2}}}},
     {.04f,1,.03f,.46f},{.63f,.6f,.33f,.44f},{.12f,.41f,.88f,.47f},{{{E::flanger,.19f,.36f}}},{K::minor,5}},
    {{"voxfx.sweep.wire-wind","Wire Wind","Forest Psy","Hollow",R::sweep,M::loop,false,60,0,1.33f,150,1,false,0,"Repeating noise-only notch movement"},
     {{{F::riser,.76f,-.47f,0,1,0,.3f,{T::notch,530,.76f,4}},{F::riser,.63f,.4f,.14f,.7f,0,-.59f,{T::bandpass,1680,.94f,2}},
       {F::riser,.41f,.21f,.57f,.43f,0,.47f,{T::highpass,3100,.72f,2}}}},
     {.17f,.9f,.15f,.62f},{.5f,.5f,.5f,.5f},{.72f,.27f,.72f,.49f},{{{E::phaser,.34f,.59f}}}},
    {{"voxfx.sweep.amber-visor","Amber Visor","Goa Psy","Liquid",R::sweep,M::gate,true,61,0,1.7f,146,1,false,0,"Nasal flyby into an acid phrase"},
     {{{F::formant,.88f,-.16f,0,1,0,.7f,{T::bandpass,1000,.82f,2}},{F::squelch,.55f,.33f,.19f,.64f,-5,-.51f,{T::lowpass,4000,.95f,2}},
       {F::whoosh,.41f,-.34f,.34f,.65f,0,.27f,{T::highpass,380,.78f,2}}}},
     {.15f,.91f,.06f,.58f},{.35f,.71f,.43f,.53f},{.75f,.51f,.21f,.67f},{{{E::flanger,.24f,.62f},{E::ring,.12f,.2f}}},{K::phrygian,1}},
    // Zaps — uphill tiny chirp, two-step exchange, and a low hive jab.
    {{"voxfx.zap.ion-needle","Ion Needle","Hi-Tech","Electric",R::zap,M::oneShot,true,78,0,.14f,184,1,false,0,"Upward micro-chirp at the end of a fill"},
     {{{F::laser,.94f,-.18f,0,.99f,0,-.57f,{T::highpass,760,.66f,2}},{F::glitch,.41f,.46f,.46f,.54f,12,.67f,{T::lowpass,5900,.72f,2}}}},
     {.19f,1,0,.39f},{.28f,.59f,.76f,.56f},{.42f,.64f,.46f,.4f},{{{E::ring,.27f,.36f}}}},
    {{"voxfx.zap.delta-dialogue","Delta Dialogue","Goa Psy","Curious",R::zap,M::oneShot,true,54,0,.64f,144,1,false,0,"Two-note acid question and answer"},
     {{{F::squelch,.93f,-.37f,0,.52f,0,.51f,{T::lowpass,3800,1.23f,2}},{F::formant,.72f,.44f,.48f,.52f,7,-.37f,{T::bandpass,1510,.83f,2}}}},
     {.67f,.89f,.18f,.56f},{.69f,.4f,.55f,.42f},{.33f,.5f,.69f,.52f},{{{E::drive,.27f,.69f},{E::delay,.15f,.14f}}},{K::phrygian,6}},
    {{"voxfx.zap.hive-jab","Hive Jab","Dark Forest","Organic",R::zap,M::gate,true,45,0,.31f,154,1,false,0,"Low wet jab between bass hits"},
     {{{F::formant,.89f,.05f,0,.94f,-5,-.62f,{T::bandpass,710,1.17f,2}},{F::metallic,.56f,-.23f,.16f,.65f,-12,.63f,{T::lowpass,1870,.72f,4}},
       {F::granular,.39f,.39f,.28f,.72f,0,-.34f,{T::lowpass,4900,.71f,2}}}},
     {.56f,1,0,.23f},{.43f,.61f,.3f,.33f},{.54f,.46f,.58f,.56f},{{{E::wavefold,.14f,.26f},{E::lowpass,.28f,.57f}}},{K::dorian,9}},
    // Rhythms — exact authored phrase notes, accents, probabilities and ratchets.
    {{"voxfx.rhythm.ribbon-code","Ribbon Code","Progressive Tech","Precise",R::rhythm,M::sequence,true,52,0,.13f,131,1,false,0,"Four-bar syncopated turnaround"},
     {{{F::metallic,.68f,.29f,0,.7f,0,-.42f,{T::bandpass,1450,.86f,2}},{F::impact,.79f,-.13f,0,.54f,-12,-.18f,{T::lowpass,980,.7f,4}}}},
     {.96f,.56f,0,.21f},{.57f,.45f,.39f,.31f},{.52f,.39f,.58f,.49f},{{{E::drive,.32f,.48f},{E::delay,.12f,.13f}}},{K::minor,4},
     {G::sixteenth,4,.19f,{{{0,52,119},{3,55,80,96},{7,52,103},{14,59,93,100,2},{16,52,113},{20,50,98},{29,57,81,90},
                         {32,52,121},{35,55,91},{43,57,108,100,2},{48,52,115},{54,64,79,97},{57,62,96},{61,59,108,100,3},{63,52,121}}}}},
    {{"voxfx.rhythm.tokamak-ticks","Tokamak Ticks","Hi-Tech","Nervous",R::rhythm,M::sequence,true,69,0,.084f,184,1,false,0,"Triplet micro-ticks with four-hit ratchets"},
     {{{F::glitch,.82f,-.37f,0,1,0,.63f,{T::highpass,1100,.69f,2}},{F::laser,.44f,.39f,.21f,.64f,12,.28f,{T::bandpass,2900,.75f,2}},
       {F::riser,.37f,.15f,0,.31f,0,.24f,{T::highpass,3840,.77f,2}}}},
     {.8f,.68f,0,.29f},{.61f,.49f,.77f,.29f},{.36f,.67f,.54f,.62f},{{{E::bitcrush,.26f,.52f},{E::ring,.22f,.37f}}},{K::chromatic,0},
     {G::sixteenthTriplet,2,.08f,{{{0,69,115},{1,76,91,100,4},{5,71,100},{9,68,87,90,3},{13,76,95},{17,72,103,100,2},
                                 {21,74,111},{23,81,97,95,4},{24,69,122},{29,66,92},{34,75,113,100,3},{38,70,86},{44,76,99,93,4},{47,69,117}}}}},
    {{"voxfx.rhythm.equinox-call","Equinox Call","Uplifting","Warm",R::rhythm,M::sequence,true,65,0,.23f,138,1,false,0,"Bright triplet chord pickup"},
     {{{F::formant,.78f,-.28f,0,.92f,0,.41f,{T::lowpass,4300,.66f,2}},{F::texture,.59f,.32f,.07f,.87f,7,.34f,{T::highpass,191,.73f,2}},
       {F::shepard,.32f,.18f,.3f,.7f,12,-.44f,{T::lowpass,2990,.7f,2}}}},
     {.05f,.93f,.04f,.37f},{.48f,.54f,.5f,.39f},{.41f,.57f,.66f,.59f},{{{E::chorus,.24f,.4f},{E::reverb,.2f,.33f}}},{K::major,5},
     {G::eighthTriplet,1,.06f,{{{0,65,105},{3,69,90},{4,72,104},{6,77,119},{9,76,87,100,2},{10,72,98},{11,69,83}}}}},
    // Atmospheres — rim-lit choir, procedural grain bed, and air without a key.
    {{"voxfx.atmosphere.polar-halo","Polar Halo","Daylight Trance","Serene",R::atmosphere,M::continuous,true,67,0,5.2f,130,1,false,0,"Wide soft choir beneath a breakdown"},
     {{{F::formant,.69f,-.29f,0,1,0,-.5f,{T::lowpass,2700,.69f,2}},{F::texture,.78f,.31f,.01f,.97f,-12,.52f,{T::highpass,109,.69f,2}},
       {F::shepard,.43f,.17f,.36f,.64f,7,-.56f,{T::bandpass,1320,.62f,2}}}},
     {.09f,.83f,.12f,.63f},{.49f,.54f,.5f,.77f},{.48f,.61f,.43f,.69f},{{{E::chorus,.23f,.6f},{E::reverb,.33f,.53f}}},{K::major,7}},
    {{"voxfx.atmosphere.black-peat","Black Peat","Dark Forest","Earthy",R::atmosphere,M::loop,true,41,0,3.25f,154,1,false,0,"Looping granular bed with low modal haze"},
     {{{F::granular,.82f,.26f,0,1,-5,-.69f,{T::lowpass,1850,.7f,4}},{F::metallic,.44f,-.34f,0,.83f,-12,.59f,{T::bandpass,580,.93f,2}},
       {F::texture,.61f,.04f,.21f,.72f,0,-.49f,{T::lowpass,2800,.7f,2}}}},
     {.28f,.92f,.27f,.56f},{.46f,.53f,.46f,.48f},{.53f,.33f,.53f,.63f},{{{E::phaser,.26f,.46f},{E::reverb,.24f,.48f}}},{K::phrygian,5}},
    {{"voxfx.atmosphere.skylight-dust","Skylight Dust","Balearic Trance","Airy",R::atmosphere,M::continuous,false,60,0,7.1f,122,1,false,0,"Keyless high window air"},
     {{{F::riser,.82f,-.32f,0,1,0,-.6f,{T::bandpass,970,.65f,2}},{F::riser,.66f,.41f,.0f,1,0,.2f,{T::highpass,1750,.61f,2}},
       {F::riser,.38f,.08f,.51f,.49f,0,.47f,{T::notch,630,.83f,2}}}},
     {.13f,.66f,.08f,.72f},{.5f,.5f,.5f,.5f},{.42f,.55f,.47f,.69f},{{{E::flanger,.13f,.55f},{E::reverb,.35f,.71f}}}},
    // Stingers — open fifth chime, cut-off alarm, and a destabilizing fill.
    {{"voxfx.stinger.crest-mark","Crest Mark","Uplifting","Celebratory",R::stinger,M::oneShot,true,72,0,1.07f,140,1,false,0,"Open-fifth chime for a section marker"},
     {{{F::metallic,.79f,-.19f,0,.83f,0,-.53f,{T::lowpass,4200,.7f,2}},{F::formant,.65f,.31f,.16f,.84f,7,.32f,{T::highpass,390,.7f,2}},
       {F::impact,.45f,0,0,.47f,-12,-.19f,{T::lowpass,1200,.73f,2}}}},
     {.77f,.96f,0,.21f},{.49f,.51f,.48f,.44f},{.43f,.6f,.52f,.41f},{{{E::chorus,.2f,.33f},{E::reverb,.33f,.56f}}},{K::major,0}},
    {{"voxfx.stinger.hard-stop","Hard Stop","Dark Psy","Ominous",R::stinger,M::gate,true,48,0,.52f,156,1,false,0,"Gated dissonant break marker"},
     {{{F::squelch,.78f,-.33f,0,.82f,0,-.65f,{T::bandpass,1150,1.1f,2}},{F::metallic,.64f,.39f,.09f,.84f,6,.65f,{T::lowpass,2500,.71f,2}},
       {F::impact,.71f,0,.58f,.42f,-12,.44f,{T::lowpass,640,.82f,4}}}},
     {.48f,1,0,.64f},{.6f,.38f,.29f,.51f},{.23f,.64f,.5f,.61f},{{{E::wavefold,.2f,.43f},{E::gate,.44f,.62f}}}},
    {{"voxfx.stinger.parallax-fill","Parallax Fill","Hi-Tech","Unstable",R::stinger,M::oneShot,true,62,0,.95f,178,1,false,0,"Stereo glitch fill before a section switch"},
     {{{F::glitch,.79f,-.4f,0,.42f,12,.69f,{T::highpass,820,.67f,2}},{F::whoosh,.43f,.43f,.27f,.61f,0,.49f,{T::bandpass,1640,.78f,2}},
       {F::laser,.66f,.34f,.43f,.55f,-7,-.31f,{T::lowpass,2850,.76f,2}},{F::impact,.81f,0,.78f,.22f,-12,.45f,{T::lowpass,610,.72f,4}}}},
     {.71f,1,.06f,.78f},{.77f,.41f,.34f,.5f},{.22f,.75f,.48f,.6f},{{{E::bitcrush,.22f,.48f},{E::flanger,.17f,.49f},{E::drive,.25f,.38f}}},{K::minor,2}},
};
