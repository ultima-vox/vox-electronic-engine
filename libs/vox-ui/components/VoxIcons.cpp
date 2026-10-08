#include "VoxIcons.h"

#include <cmath>

namespace vox::ui::icons {

namespace {

// Every glyph is authored in this box. Scaling happens at the very end, so the
// numbers below can stay readable and unit-consistent.
constexpr float box = 100.0f;

constexpr float powerStroke = 9.0f;
constexpr float chevronStroke = 11.0f;
constexpr float plusStroke = 11.0f;
constexpr float outlineStroke = 7.5f;
constexpr float diceStroke = 8.0f;

juce::Point<float> polar (const float radius, const float radians)
{
    // Angles are measured clockwise from 12 o'clock, matching
    // juce::Path::addCentredArc, so icon helpers and knob maths agree.
    return { box * 0.5f + radius * std::sin (radians),
             box * 0.5f - radius * std::cos (radians) };
}

juce::Path makeChevron (const juce::Point<float> a, const juce::Point<float> b,
                        const juce::Point<float> c)
{
    juce::Path path;
    path.startNewSubPath (a);
    path.lineTo (b);
    path.lineTo (c);
    return path;
}

// A closed, stroke-only dot: a degenerate segment rendered with a round end cap
// becomes a disc of the family stroke width, so a pip stays part of one stroked
// path instead of needing a second fill pass.
void addDot (juce::Path& path, const float x, const float y)
{
    path.startNewSubPath (x, y);
    path.lineTo (x + 0.01f, y);
}

juce::Path makePower()
{
    // Circle with a gap at the top plus a short vertical stub through the gap.
    // Measured from reference/detail/rack-top.png and the accepted SOUND render:
    // 15 x 17 px at a 2 px stroke, gap half-angle ~26 degrees, stub reaching
    // ~1 px above the ring and down to the body centre.
    constexpr float centreX = 50.0f;
    constexpr float centreY = 56.0f;
    constexpr float radius = 38.0f;
    constexpr float gapHalfAngle = 0.4538f; // 26 degrees

    juce::Path path;
    path.addCentredArc (centreX, centreY, radius, radius, 0.0f,
                        gapHalfAngle, juce::MathConstants<float>::twoPi - gapHalfAngle,
                        true);
    path.startNewSubPath (centreX, 10.0f);
    path.lineTo (centreX, 50.0f);
    return path;
}

juce::Path makeGear()
{
    constexpr int teeth = 8;
    constexpr float outerRadius = 45.0f;
    constexpr float rootRadius = 33.0f;
    constexpr float hubRadius = 14.0f;
    constexpr float pitch = juce::MathConstants<float>::twoPi / static_cast<float> (teeth);
    constexpr float toothHalf = pitch * 0.17f;
    constexpr float lead = pitch * 0.07f;

    juce::Path path;
    for (int i = 0; i < teeth; ++i)
    {
        const auto centre = static_cast<float> (i) * pitch;
        const auto a = polar (rootRadius, centre - toothHalf - lead);
        const auto b = polar (outerRadius, centre - toothHalf);
        const auto c = polar (outerRadius, centre + toothHalf);
        const auto d = polar (rootRadius, centre + toothHalf + lead);

        if (i == 0)
            path.startNewSubPath (a);
        else
            path.lineTo (a);

        path.lineTo (b);
        path.lineTo (c);
        path.lineTo (d);
    }
    path.closeSubPath();

    path.addEllipse (box * 0.5f - hubRadius, box * 0.5f - hubRadius,
                     hubRadius * 2.0f, hubRadius * 2.0f);
    return path;
}

juce::Path makePencil()
{
    // Authored directly on the 45 degree diagonal so no rotation/scale pass is
    // needed and the stroke weight stays exactly nominal.
    const juce::Point<float> tip { 20.0f, 80.0f };
    const juce::Point<float> direction { 0.70710678f, -0.70710678f };
    const juce::Point<float> across { 0.70710678f, 0.70710678f };
    constexpr float halfWidth = 9.0f;

    const auto along = [&] (const float distance)
    {
        return tip + direction * distance;
    };
    const auto offset = [&] (const juce::Point<float> point, const float amount)
    {
        return point + across * amount;
    };

    const auto shoulder = along (16.0f);
    const auto tail = along (76.0f);
    const auto collar = along (64.0f);

    juce::Path path;
    path.startNewSubPath (tip);
    path.lineTo (offset (shoulder, halfWidth));
    path.lineTo (offset (tail, halfWidth));
    path.lineTo (offset (tail, -halfWidth));
    path.lineTo (offset (shoulder, -halfWidth));
    path.closeSubPath();

    path.startNewSubPath (offset (collar, halfWidth));
    path.lineTo (offset (collar, -halfWidth));
    return path;
}

juce::Path makeHeart()
{
    juce::Path path;
    path.startNewSubPath (50.0f, 84.0f);
    path.cubicTo (10.0f, 54.0f, 10.0f, 20.0f, 31.0f, 15.0f);
    path.cubicTo (42.0f, 12.5f, 49.0f, 21.0f, 50.0f, 31.0f);
    path.cubicTo (51.0f, 21.0f, 58.0f, 12.5f, 69.0f, 15.0f);
    path.cubicTo (90.0f, 20.0f, 90.0f, 54.0f, 50.0f, 84.0f);
    path.closeSubPath();
    return path;
}

juce::Path makeDice()
{
    juce::Path path;
    path.addRoundedRectangle (12.0f, 12.0f, 76.0f, 76.0f, 14.0f);
    addDot (path, 33.0f, 33.0f);
    addDot (path, 67.0f, 33.0f);
    addDot (path, 50.0f, 50.0f);
    addDot (path, 33.0f, 67.0f);
    addDot (path, 67.0f, 67.0f);
    return path;
}

juce::Path makeCube()
{
    const juce::Point<float> top { 50.0f, 8.0f };
    const juce::Point<float> upperRight { 88.0f, 30.0f };
    const juce::Point<float> lowerRight { 88.0f, 70.0f };
    const juce::Point<float> bottom { 50.0f, 92.0f };
    const juce::Point<float> lowerLeft { 12.0f, 70.0f };
    const juce::Point<float> upperLeft { 12.0f, 30.0f };
    const juce::Point<float> centre { 50.0f, 52.0f };

    juce::Path path;
    path.startNewSubPath (top);
    path.lineTo (upperRight);
    path.lineTo (lowerRight);
    path.lineTo (bottom);
    path.lineTo (lowerLeft);
    path.lineTo (upperLeft);
    path.closeSubPath();

    path.startNewSubPath (upperLeft);
    path.lineTo (centre);
    path.lineTo (upperRight);
    path.startNewSubPath (centre);
    path.lineTo (bottom);
    return path;
}

juce::Path makeKeyboardLayout()
{
    juce::Path path;
    path.addRoundedRectangle (6.0f, 24.0f, 88.0f, 52.0f, 7.0f);
    for (const auto x : { 22.0f, 36.0f, 50.0f, 64.0f, 78.0f })
    {
        path.startNewSubPath (x, 24.0f);
        path.lineTo (x, 50.0f);
    }
    return path;
}

juce::Path makeMatrixGrid()
{
    juce::Path path;
    path.addRoundedRectangle (8.0f, 14.0f, 84.0f, 72.0f, 6.0f);
    path.startNewSubPath (36.0f, 14.0f);
    path.lineTo (36.0f, 86.0f);
    path.startNewSubPath (8.0f, 38.0f);
    path.lineTo (92.0f, 38.0f);
    path.startNewSubPath (8.0f, 62.0f);
    path.lineTo (92.0f, 62.0f);
    return path;
}

juce::Path makePlay()
{
    juce::Path path;
    path.startNewSubPath (26.0f, 12.0f);
    path.lineTo (86.0f, 50.0f);
    path.lineTo (26.0f, 88.0f);
    path.closeSubPath();
    return path;
}

juce::Path makeStop()
{
    juce::Path path;
    path.addRoundedRectangle (18.0f, 18.0f, 64.0f, 64.0f, 8.0f);
    return path;
}

} // namespace

juce::Path make (const Icon id)
{
    switch (id)
    {
        case Icon::chevronLeft:
            return makeChevron ({ 62.0f, 16.0f }, { 34.0f, 50.0f }, { 62.0f, 84.0f });
        case Icon::chevronRight:
            return makeChevron ({ 38.0f, 16.0f }, { 66.0f, 50.0f }, { 38.0f, 84.0f });
        case Icon::chevronDown:
            return makeChevron ({ 18.0f, 36.0f }, { 50.0f, 66.0f }, { 82.0f, 36.0f });
        case Icon::power:
            return makePower();
        case Icon::plus:
        {
            juce::Path path;
            path.startNewSubPath (16.0f, 50.0f);
            path.lineTo (84.0f, 50.0f);
            path.startNewSubPath (50.0f, 16.0f);
            path.lineTo (50.0f, 84.0f);
            return path;
        }
        case Icon::gear:
            return makeGear();
        case Icon::pencil:
            return makePencil();
        case Icon::heart:
            return makeHeart();
        case Icon::dice:
            return makeDice();
        case Icon::cube:
            return makeCube();
        case Icon::keyboardLayout:
            return makeKeyboardLayout();
        case Icon::play:
            return makePlay();
        case Icon::stop:
            return makeStop();
        case Icon::matrixGrid:
            return makeMatrixGrid();
    }

    return {};
}

juce::Path make (const Icon id, const float size)
{
    auto path = make (id);
    if (size > 0.0f)
        path.applyTransform (juce::AffineTransform::scale (size / box));
    return path;
}

bool isFilled (const Icon id) noexcept
{
    return id == Icon::play || id == Icon::stop;
}

float nominalStroke (const Icon id) noexcept
{
    switch (id)
    {
        case Icon::power:  return powerStroke / box;
        case Icon::plus:   return plusStroke / box;
        case Icon::dice:   return diceStroke / box;
        case Icon::chevronLeft:
        case Icon::chevronRight:
        case Icon::chevronDown:
            return chevronStroke / box;
        case Icon::gear:
        case Icon::pencil:
        case Icon::heart:
        case Icon::cube:
        case Icon::keyboardLayout:
        case Icon::matrixGrid:
            return outlineStroke / box;
        case Icon::play:
        case Icon::stop:
            return 0.0f;
    }

    return outlineStroke / box;
}

void draw (juce::Graphics& g, const Icon id, const juce::Rectangle<float> area,
           const juce::Colour colour, const float alpha)
{
    const auto side = juce::jmin (area.getWidth(), area.getHeight());
    if (side < 1.0f || alpha <= 0.0f)
        return;

    auto path = make (id, side);
    path.applyTransform (juce::AffineTransform::translation (
        area.getCentreX() - side * 0.5f, area.getCentreY() - side * 0.5f));

    g.setColour (colour.withMultipliedAlpha (juce::jlimit (0.0f, 1.0f, alpha)));

    if (isFilled (id))
    {
        g.fillPath (path);
        return;
    }

    const auto weight = juce::jmax (1.0f, side * nominalStroke (id));
    g.strokePath (path, juce::PathStrokeType (weight,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

std::unique_ptr<juce::Drawable> makeDrawable (const Icon id, const juce::Colour colour,
                                              const float size)
{
    auto drawable = std::make_unique<juce::DrawablePath>();
    drawable->setPath (make (id, size));

    if (isFilled (id))
    {
        drawable->setFill (juce::FillType (colour));
    }
    else
    {
        drawable->setStrokeType (juce::PathStrokeType (
            juce::jmax (1.0f, size * nominalStroke (id)),
            juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
        drawable->setStrokeFill (juce::FillType (colour));
    }

    return drawable;
}

} // namespace vox::ui::icons
